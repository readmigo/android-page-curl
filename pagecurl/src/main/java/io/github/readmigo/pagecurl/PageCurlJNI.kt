package io.github.readmigo.pagecurl

import android.graphics.Bitmap

/**
 * JNI bridge to the native OpenGL ES page curl renderer.
 * All native* methods must be called on the GL thread.
 */
internal object PageCurlJNI {

    init {
        System.loadLibrary("pagecurl")
    }

    const val TEX_CURRENT = 0
    const val TEX_NEXT    = 1
    const val TEX_PREV    = 2

    /** Allocate a native PageCurlRenderer. Returns an opaque pointer (handle). */
    @JvmStatic external fun nativeCreate(): Long

    /** Called from GLSurfaceView.Renderer.onSurfaceCreated. */
    @JvmStatic external fun nativeSurfaceCreated(ptr: Long)

    /** Called from GLSurfaceView.Renderer.onSurfaceChanged. */
    @JvmStatic external fun nativeSurfaceChanged(ptr: Long, width: Int, height: Int)

    /**
     * Upload a Bitmap (must be ARGB_8888) to the given texture slot.
     * [slot] is one of TEX_CURRENT / TEX_NEXT / TEX_PREV.
     */
    @JvmStatic external fun nativeSetBitmap(ptr: Long, slot: Int, bitmap: Bitmap)

    /**
     * Render a forward page turn (current page curls right-to-left).
     * [foldX]     normalized fold position [0.0, 1.0]; 1.0 = not curled, 0.0 = fully turned.
     * [foldSlope] diagonal tilt of fold line; 0 = vertical, positive = bottom-right corner first.
     */
    @JvmStatic external fun nativeDrawForward(ptr: Long, foldX: Float, foldSlope: Float)

    /**
     * Render a backward page turn (previous page slides in from left).
     * [foldX]     normalized fold position [0.0, 1.0]; 0.0 = not curled, 1.0 = fully turned.
     * [foldSlope] diagonal tilt (mirrored internally for the left-side fold).
     */
    @JvmStatic external fun nativeDrawBackward(ptr: Long, foldX: Float, foldSlope: Float)

    /** Free the native renderer. Must be called from the GL thread. */
    @JvmStatic external fun nativeDestroy(ptr: Long)
}
