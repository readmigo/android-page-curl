package io.github.readmigo.pagecurl

import android.graphics.Bitmap
import android.opengl.GLSurfaceView
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.spring
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.util.VelocityTracker
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.viewinterop.AndroidView
import kotlinx.coroutines.launch
import java.util.concurrent.ConcurrentLinkedQueue
import java.util.concurrent.atomic.AtomicLong
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10
import kotlin.math.abs

private const val COMPLETION_THRESHOLD = 0.35f
private const val VELOCITY_THRESHOLD   = 500f

// ---------------------------------------------------------------------------
// Internal render state – bridges Compose thread and GL thread safely
// ---------------------------------------------------------------------------

private class PageCurlRenderState {
    /** Normalized fold position [0,1]: 1.0 = fully flat, 0.0 = fully turned (forward). */
    @Volatile var foldX: Float = 1.0f
    /** True = forward curl (right-to-left), false = backward curl (left-to-right). */
    @Volatile var isForward: Boolean = true
    /** Background color ARGB for GL clear. */
    @Volatile var bgColor: Int = android.graphics.Color.WHITE

    // Bitmaps queued for upload to the GL thread.
    // Triple<slot, bitmap, generationId>
    val pendingTextures: ConcurrentLinkedQueue<Pair<Int, Bitmap>> = ConcurrentLinkedQueue()
}

// ---------------------------------------------------------------------------
// GL Renderer
// ---------------------------------------------------------------------------

private class PageCurlGLRenderer(
    private val state: PageCurlRenderState,
    private val nativePtr: AtomicLong
) : GLSurfaceView.Renderer {

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        val ptr = nativePtr.get()
        if (ptr != 0L) PageCurlJNI.nativeSurfaceCreated(ptr)
    }

    override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
        val ptr = nativePtr.get()
        if (ptr != 0L) PageCurlJNI.nativeSurfaceChanged(ptr, width, height)
    }

    override fun onDrawFrame(gl: GL10?) {
        val ptr = nativePtr.get()
        if (ptr == 0L) return

        // Upload any pending bitmaps
        while (true) {
            val (slot, bitmap) = state.pendingTextures.poll() ?: break
            if (!bitmap.isRecycled) {
                // Ensure ARGB_8888 format for the JNI layer
                val src = if (bitmap.config == Bitmap.Config.ARGB_8888) {
                    bitmap
                } else {
                    bitmap.copy(Bitmap.Config.ARGB_8888, false)
                }
                PageCurlJNI.nativeSetBitmap(ptr, slot, src)
                if (src !== bitmap) src.recycle()
            }
        }

        val foldX    = state.foldX
        val forward  = state.isForward

        if (forward) {
            PageCurlJNI.nativeDrawForward(ptr, foldX)
        } else {
            // For backward curl foldX encodes progress from 0→1 (fold moves left→right)
            PageCurlJNI.nativeDrawBackward(ptr, foldX)
        }
    }
}

// ---------------------------------------------------------------------------
// Public Composable
// ---------------------------------------------------------------------------

/**
 * A realistic OpenGL ES page-curl container.
 *
 * Callers supply a list of [Bitmap]s (one per page). Rendering, gesture
 * handling, and curl animation are handled internally via a [GLSurfaceView].
 *
 * @param pages           Pre-rendered page bitmaps.
 * @param backgroundColor Background color shown before bitmaps are uploaded.
 * @param startFromLastPage Open at the last page instead of the first.
 * @param onPageChanged   Invoked (1-based currentPage, totalPages) after every turn.
 * @param onReachStart    Invoked when the user tries to go before page 1.
 * @param onReachEnd      Invoked when the user tries to go past the last page.
 * @param onTap           Invoked on a tap in the centre third of the screen.
 */
@Composable
fun PageCurlContainer(
    pages: List<Bitmap>,
    backgroundColor: Color = Color.White,
    modifier: Modifier = Modifier,
    startFromLastPage: Boolean = false,
    onPageChanged: (currentPage: Int, totalPages: Int) -> Unit = { _, _ -> },
    onReachStart: () -> Unit = {},
    onReachEnd: () -> Unit = {},
    onTap: () -> Unit = {}
) {
    val scope     = rememberCoroutineScope()
    val pageCount = pages.size

    var currentPage by remember(pageCount, startFromLastPage) {
        mutableIntStateOf(if (startFromLastPage && pageCount > 0) pageCount - 1 else 0)
    }

    // curlProgress: 0 = not curled, 1 = fully turned
    val curlProgress = remember { Animatable(0f) }
    var curlForward  by remember { mutableStateOf(true) }

    val renderState = remember { PageCurlRenderState() }
    val nativePtr   = remember { AtomicLong(PageCurlJNI.nativeCreate()) }

    // Report initial page count
    LaunchedEffect(currentPage, pageCount) {
        if (pageCount > 0) onPageChanged(currentPage + 1, pageCount)
    }

    // Push background color to render state
    LaunchedEffect(backgroundColor) {
        renderState.bgColor = backgroundColor.toArgb()
    }

    // Upload bitmaps to render state whenever pages change
    LaunchedEffect(pages, currentPage) {
        if (pages.isEmpty()) return@LaunchedEffect
        // Upload current, next, prev
        fun enqueue(slot: Int, index: Int) {
            pages.getOrNull(index)?.let { renderState.pendingTextures.add(Pair(slot, it)) }
        }
        enqueue(PageCurlJNI.TEX_CURRENT, currentPage)
        enqueue(PageCurlJNI.TEX_NEXT,    currentPage + 1)
        enqueue(PageCurlJNI.TEX_PREV,    currentPage - 1)
    }

    // Bridge Compose animation state → GL render state
    LaunchedEffect(Unit) {
        snapshotFlow { Pair(curlProgress.value, curlForward) }.collect { (progress, forward) ->
            renderState.isForward = forward
            renderState.foldX = if (forward) {
                // Forward: foldX goes 1.0 → 0.0 as progress goes 0 → 1
                1.0f - progress
            } else {
                // Backward: foldX goes 0.0 → 1.0 as progress goes 0 → 1
                progress
            }
        }
    }

    // Cleanup native renderer when Composable leaves composition
    DisposableEffect(Unit) {
        onDispose {
            val ptr = nativePtr.getAndSet(0L)
            if (ptr != 0L) {
                // nativeDestroy must be called on the GL thread; GLSurfaceView handles
                // queueing via queueEvent. Store pointer temporarily for the view to call.
                PageCurlJNI.nativeDestroy(ptr)
            }
        }
    }

    // Animation helpers
    fun animateToComplete(forward: Boolean) {
        scope.launch {
            curlProgress.animateTo(1f, spring(Spring.DampingRatioMediumBouncy, Spring.StiffnessMedium))
            if (forward) {
                if (currentPage < pageCount - 1) currentPage++ else onReachEnd()
            } else {
                if (currentPage > 0) currentPage-- else onReachStart()
            }
            curlProgress.snapTo(0f)
        }
    }

    fun animateToCancel() {
        scope.launch {
            curlProgress.animateTo(0f, spring(Spring.DampingRatioMediumBouncy, Spring.StiffnessMedium))
        }
    }

    val context = LocalContext.current

    Box(
        modifier = modifier
            .fillMaxSize()
            .onSizeChanged { /* size observed inside AndroidView */ }
            .pointerInput(pageCount, currentPage) {
                val velocityTracker = VelocityTracker()
                detectDragGestures(
                    onDragStart = { startOffset ->
                        velocityTracker.resetTracking()
                        curlForward = startOffset.x > size.width / 2
                        scope.launch { curlProgress.snapTo(0f) }
                    },
                    onDrag = { change, dragAmount ->
                        change.consume()
                        velocityTracker.addPosition(change.uptimeMillis, change.position)
                        val w = size.width.toFloat()
                        if (w > 0) {
                            val delta = if (curlForward) -dragAmount.x / w else dragAmount.x / w
                            scope.launch {
                                curlProgress.snapTo((curlProgress.value + delta).coerceIn(0f, 1f))
                            }
                        }
                    },
                    onDragEnd = {
                        val vel = try { velocityTracker.calculateVelocity() } catch (_: Exception) { null }
                        val vx  = vel?.x ?: 0f
                        val p   = curlProgress.value

                        val shouldComplete = if (abs(vx) > VELOCITY_THRESHOLD) {
                            if (curlForward) vx < 0 else vx > 0
                        } else {
                            p > COMPLETION_THRESHOLD
                        }

                        val canTurn = if (curlForward) currentPage < pageCount - 1 else currentPage > 0

                        if (shouldComplete && (canTurn || p > COMPLETION_THRESHOLD)) {
                            animateToComplete(curlForward)
                        } else {
                            animateToCancel()
                        }
                    },
                    onDragCancel = { animateToCancel() }
                )
            }
            .pointerInput(pageCount, currentPage) {
                detectTapGestures(
                    onTap = { offset ->
                        val third = size.width / 3
                        when {
                            offset.x < third -> {
                                if (currentPage > 0) {
                                    curlForward = false
                                    animateToComplete(false)
                                } else {
                                    onReachStart()
                                }
                            }
                            offset.x > third * 2 -> {
                                if (currentPage < pageCount - 1) {
                                    curlForward = true
                                    animateToComplete(true)
                                } else {
                                    onReachEnd()
                                }
                            }
                            else -> onTap()
                        }
                    }
                )
            }
    ) {
        AndroidView(
            modifier = Modifier.fillMaxSize(),
            factory = { ctx ->
                val renderer = PageCurlGLRenderer(renderState, nativePtr)
                GLSurfaceView(ctx).apply {
                    setEGLContextClientVersion(3)
                    setRenderer(renderer)
                    renderMode = GLSurfaceView.RENDERMODE_CONTINUOUSLY
                }
            },
            update = { glView ->
                glView.requestRender()
            }
        )
    }
}
