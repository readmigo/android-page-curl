#include <jni.h>
#include <android/bitmap.h>
#include <android/log.h>
#include "page_curl_renderer.h"

#define LOG_TAG "PageCurlJNI"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Helper: lock an Android Bitmap and call fn(rgba, width, height).
// Returns false on error.
template<typename Fn>
static bool withBitmap(JNIEnv* env, jobject bitmap, Fn fn) {
    AndroidBitmapInfo info;
    if (AndroidBitmap_getInfo(env, bitmap, &info) < 0) {
        LOGE("AndroidBitmap_getInfo failed");
        return false;
    }
    if (info.format != ANDROID_BITMAP_FORMAT_RGBA_8888) {
        LOGE("Bitmap format is not RGBA_8888 (format=%d)", info.format);
        return false;
    }
    void* pixels = nullptr;
    if (AndroidBitmap_lockPixels(env, bitmap, &pixels) < 0) {
        LOGE("AndroidBitmap_lockPixels failed");
        return false;
    }
    fn(static_cast<const uint8_t*>(pixels),
       static_cast<int>(info.width),
       static_cast<int>(info.height));
    AndroidBitmap_unlockPixels(env, bitmap);
    return true;
}

extern "C" {

JNIEXPORT jlong JNICALL
Java_io_github_readmigo_pagecurl_PageCurlJNI_nativeCreate(JNIEnv*, jclass) {
    return reinterpret_cast<jlong>(new PageCurlRenderer());
}

JNIEXPORT void JNICALL
Java_io_github_readmigo_pagecurl_PageCurlJNI_nativeSurfaceCreated(JNIEnv*, jclass, jlong ptr) {
    reinterpret_cast<PageCurlRenderer*>(ptr)->onSurfaceCreated();
}

JNIEXPORT void JNICALL
Java_io_github_readmigo_pagecurl_PageCurlJNI_nativeSurfaceChanged(JNIEnv*, jclass, jlong ptr,
                                                                    jint w, jint h) {
    reinterpret_cast<PageCurlRenderer*>(ptr)->onSurfaceChanged(w, h);
}

JNIEXPORT void JNICALL
Java_io_github_readmigo_pagecurl_PageCurlJNI_nativeSetBitmap(JNIEnv* env, jclass, jlong ptr,
                                                              jint slot, jobject bitmap) {
    auto* r = reinterpret_cast<PageCurlRenderer*>(ptr);
    withBitmap(env, bitmap, [&](const uint8_t* rgba, int w, int h) {
        r->setTexture(slot, rgba, w, h);
    });
}

JNIEXPORT void JNICALL
Java_io_github_readmigo_pagecurl_PageCurlJNI_nativeDrawForward(JNIEnv*, jclass, jlong ptr,
                                                                jfloat foldX) {
    reinterpret_cast<PageCurlRenderer*>(ptr)->drawForward(foldX);
}

JNIEXPORT void JNICALL
Java_io_github_readmigo_pagecurl_PageCurlJNI_nativeDrawBackward(JNIEnv*, jclass, jlong ptr,
                                                                 jfloat foldX) {
    reinterpret_cast<PageCurlRenderer*>(ptr)->drawBackward(foldX);
}

JNIEXPORT void JNICALL
Java_io_github_readmigo_pagecurl_PageCurlJNI_nativeDestroy(JNIEnv*, jclass, jlong ptr) {
    delete reinterpret_cast<PageCurlRenderer*>(ptr);
}

} // extern "C"
