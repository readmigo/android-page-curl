package io.github.readmigo.pagecurl

import android.graphics.Bitmap
import android.graphics.Paint
import android.graphics.PointF
import android.graphics.RectF
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.drawIntoCanvas
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.util.VelocityTracker
import androidx.compose.ui.layout.onSizeChanged
import kotlinx.coroutines.launch
import kotlin.math.abs

private const val COMPLETION_THRESHOLD = 0.35f
private const val VELOCITY_THRESHOLD = 500f

// Back-face rendering: opacity of the mirrored page content (0-255)
private const val BACK_FACE_CONTENT_ALPHA = 180
// Back-face rendering: semi-transparent white overlay for paper-back look
private const val BACK_FACE_OVERLAY_ALPHA = 50

/**
 * A realistic Canvas-based page-curl container for Jetpack Compose.
 *
 * Callers supply a list of [Bitmap]s (one per page). Rendering, gesture
 * handling, and curl animation are handled internally using Android Canvas
 * with bezier fold-line geometry.
 *
 * @param pages           Pre-rendered page bitmaps.
 * @param backgroundColor Background color shown when no page is drawn.
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
    val scope = rememberCoroutineScope()
    val pageCount = pages.size
    val bgArgb = backgroundColor.toArgb()

    var currentPage by remember(pageCount, startFromLastPage) {
        mutableIntStateOf(if (startFromLastPage && pageCount > 0) pageCount - 1 else 0)
    }

    // Page dimensions (in pixels)
    var pageW by remember { mutableFloatStateOf(0f) }
    var pageH by remember { mutableFloatStateOf(0f) }

    // ---- Curl state ----
    // The position of the page corner being dragged (null = no curl active)
    var dragCorner by remember { mutableStateOf<PointF?>(null) }
    var curlForward by remember { mutableStateOf(true) }

    // Animation state
    val animProgress = remember { Animatable(0f) }
    var animStartPt by remember { mutableStateOf(PointF(0f, 0f)) }
    var animEndPt by remember { mutableStateOf(PointF(0f, 0f)) }
    var isAnimCompleting by remember { mutableStateOf(false) }

    // Report page changes
    LaunchedEffect(currentPage, pageCount) {
        if (pageCount > 0) onPageChanged(currentPage + 1, pageCount)
    }

    // ---- Animation helpers ----
    fun animateToComplete(forward: Boolean) {
        scope.launch {
            isAnimCompleting = true
            animProgress.snapTo(0f)
            animProgress.animateTo(
                1f,
                tween(durationMillis = 300, easing = FastOutSlowInEasing)
            )
            if (forward) {
                if (currentPage < pageCount - 1) currentPage++ else onReachEnd()
            } else {
                if (currentPage > 0) currentPage-- else onReachStart()
            }
            dragCorner = null
            isAnimCompleting = false
        }
    }

    fun animateToCancel() {
        scope.launch {
            isAnimCompleting = false
            animProgress.snapTo(0f)
            animProgress.animateTo(
                1f,
                spring(Spring.DampingRatioNoBouncy, Spring.StiffnessHigh)
            )
            dragCorner = null
        }
    }

    // Reusable Paint objects (avoid allocation per frame)
    val bitmapPaint = remember { Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG) }
    val backFacePaint = remember {
        Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG).apply {
            alpha = BACK_FACE_CONTENT_ALPHA
        }
    }
    val backOverlayPaint = remember {
        Paint(Paint.ANTI_ALIAS_FLAG).apply {
            color = android.graphics.Color.argb(BACK_FACE_OVERLAY_ALPHA, 255, 255, 255)
            style = Paint.Style.FILL
        }
    }
    val shadowPaint = remember { Paint(Paint.ANTI_ALIAS_FLAG) }

    // ---- Canvas rendering ----
    androidx.compose.foundation.Canvas(
        modifier = modifier
            .fillMaxSize()
            .onSizeChanged { size ->
                pageW = size.width.toFloat()
                pageH = size.height.toFloat()
            }
            // Drag gesture
            .pointerInput(pageCount, currentPage) {
                val velocityTracker = VelocityTracker()
                detectDragGestures(
                    onDragStart = { startOffset ->
                        velocityTracker.resetTracking()
                        curlForward = startOffset.x > size.width / 2

                        // Corner starts at the page edge
                        val cornerX = if (curlForward) size.width.toFloat() else 0f
                        val cornerY = if (startOffset.y > size.height / 2) {
                            size.height.toFloat()
                        } else {
                            0f
                        }
                        dragCorner = PointF(cornerX, cornerY)
                    },
                    onDrag = { change, dragAmount ->
                        change.consume()
                        velocityTracker.addPosition(change.uptimeMillis, change.position)

                        dragCorner?.let { cp ->
                            dragCorner = PointF(
                                (cp.x + dragAmount.x).coerceIn(
                                    -size.width * 0.15f,
                                    size.width * 1.15f
                                ),
                                (cp.y + dragAmount.y).coerceIn(
                                    -size.height * 0.15f,
                                    size.height * 1.15f
                                )
                            )
                        }
                    },
                    onDragEnd = {
                        val vel = try {
                            velocityTracker.calculateVelocity()
                        } catch (_: Exception) {
                            null
                        }
                        val vx = vel?.x ?: 0f
                        val cp = dragCorner ?: return@detectDragGestures

                        // Progress: how far the corner has moved from its origin
                        val progress = if (curlForward) {
                            (size.width - cp.x) / size.width
                        } else {
                            cp.x / size.width
                        }

                        val shouldComplete = if (abs(vx) > VELOCITY_THRESHOLD) {
                            if (curlForward) vx < 0 else vx > 0
                        } else {
                            progress > COMPLETION_THRESHOLD
                        }

                        val canTurn = if (curlForward) {
                            currentPage < pageCount - 1
                        } else {
                            currentPage > 0
                        }

                        // Set up animation endpoints
                        animStartPt = PointF(cp.x, cp.y)
                        if (shouldComplete && canTurn) {
                            animEndPt = if (curlForward) {
                                PointF(-size.width * 0.3f, cp.y * 0.5f + size.height * 0.25f)
                            } else {
                                PointF(size.width * 1.3f, cp.y * 0.5f + size.height * 0.25f)
                            }
                            animateToComplete(curlForward)
                        } else {
                            val originX = if (curlForward) size.width.toFloat() else 0f
                            val originY = if (cp.y > size.height / 2) {
                                size.height.toFloat()
                            } else {
                                0f
                            }
                            animEndPt = PointF(originX, originY)
                            animateToCancel()
                        }
                    },
                    onDragCancel = {
                        val cp = dragCorner ?: return@detectDragGestures
                        animStartPt = PointF(cp.x, cp.y)
                        val originX = if (curlForward) pageW else 0f
                        val originY = if (cp.y > pageH / 2) pageH else 0f
                        animEndPt = PointF(originX, originY)
                        animateToCancel()
                    }
                )
            }
            // Tap gesture
            .pointerInput(pageCount, currentPage) {
                detectTapGestures(
                    onTap = { offset ->
                        val third = size.width / 3
                        when {
                            offset.x < third -> {
                                if (currentPage > 0) {
                                    curlForward = false
                                    animStartPt = PointF(0f, size.height.toFloat())
                                    animEndPt = PointF(
                                        size.width * 1.3f,
                                        size.height * 0.25f
                                    )
                                    animateToComplete(false)
                                } else {
                                    onReachStart()
                                }
                            }

                            offset.x > third * 2 -> {
                                if (currentPage < pageCount - 1) {
                                    curlForward = true
                                    animStartPt = PointF(
                                        size.width.toFloat(),
                                        size.height.toFloat()
                                    )
                                    animEndPt = PointF(
                                        -size.width * 0.3f,
                                        size.height * 0.25f
                                    )
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
        drawIntoCanvas { canvas ->
            val nc = canvas.nativeCanvas
            val w = size.width
            val h = size.height
            val dst = RectF(0f, 0f, w, h)

            nc.drawColor(bgArgb)

            if (pages.isEmpty() || w <= 0f || h <= 0f) return@drawIntoCanvas

            // Compute effective corner position from drag or animation
            val effectiveCorner: PointF? = when {
                dragCorner != null && !animProgress.isRunning -> dragCorner
                animProgress.isRunning -> {
                    val t = animProgress.value
                    PointF(
                        animStartPt.x + (animEndPt.x - animStartPt.x) * t,
                        animStartPt.y + (animEndPt.y - animStartPt.y) * t
                    )
                }

                else -> null
            }

            if (effectiveCorner == null) {
                // No curl: draw current page flat
                pages.getOrNull(currentPage)?.let { bmp ->
                    nc.drawBitmap(bmp, null, dst, bitmapPaint)
                }
                return@drawIntoCanvas
            }

            // Determine the original corner position
            val originCorner = if (curlForward) {
                val cy = if (effectiveCorner.y < h / 2) 0f else h
                PointF(w, cy)
            } else {
                val cy = if (effectiveCorner.y < h / 2) 0f else h
                PointF(0f, cy)
            }

            // Calculate fold geometry
            val frame = CurlMath.calculate(effectiveCorner, originCorner, w, h)

            if (!frame.isVisible) {
                // Fold line outside page — just draw flat
                pages.getOrNull(currentPage)?.let { bmp ->
                    nc.drawBitmap(bmp, null, dst, bitmapPaint)
                }
                return@drawIntoCanvas
            }

            // ---- 5-layer rendering ----

            // Layer 1: Revealed page (next or prev) — drawn full underneath
            val revealedBmp = if (curlForward) {
                pages.getOrNull(currentPage + 1)
            } else {
                pages.getOrNull(currentPage - 1)
            }
            if (revealedBmp != null) {
                nc.save()
                nc.clipPath(frame.backPath)
                nc.drawBitmap(revealedBmp, null, dst, bitmapPaint)
                nc.restore()
            }

            // Layer 2: Cast shadow on revealed page (along fold line, curl side)
            if (frame.castShadowGradient != null && !frame.shadowRegionPath.isEmpty) {
                shadowPaint.shader = frame.castShadowGradient
                nc.save()
                nc.clipPath(frame.shadowRegionPath)
                nc.drawRect(0f, 0f, w, h, shadowPaint)
                nc.restore()
                shadowPaint.shader = null
            }

            // Layer 3: Flat part of current page (front face, uncurled)
            pages.getOrNull(currentPage)?.let { bmp ->
                nc.save()
                nc.clipPath(frame.flatPath)
                nc.drawBitmap(bmp, null, dst, bitmapPaint)
                nc.restore()
            }

            // Layer 4: Crease shadow on flat page (along fold line, flat side)
            if (frame.creaseShadowGradient != null && !frame.creaseRegionPath.isEmpty) {
                shadowPaint.shader = frame.creaseShadowGradient
                nc.save()
                nc.clipPath(frame.creaseRegionPath)
                nc.drawRect(0f, 0f, w, h, shadowPaint)
                nc.restore()
                shadowPaint.shader = null
            }

            // Layer 5: Back face of curled page (reflected across fold line)
            pages.getOrNull(currentPage)?.let { bmp ->
                nc.save()
                nc.clipPath(frame.backPath)
                nc.concat(frame.backMatrix)
                nc.drawBitmap(bmp, null, dst, backFacePaint)
                nc.restore()

                // Paper-back white overlay for realistic look
                nc.save()
                nc.clipPath(frame.backPath)
                nc.drawRect(0f, 0f, w, h, backOverlayPaint)
                nc.restore()
            }
        }
    }
}
