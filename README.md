# android-page-curl

A realistic OpenGL ES 3.0 page-curl animation library for Android, built with Jetpack Compose.

Matches the visual quality of iOS `UIPageViewController(.pageCurl)` using a cylindrical vertex-shader transform.

## Features

- **Cylindrical curl** rendered entirely in C++ with OpenGL ES 3.0
- **Compose API** — drop-in `PageCurlContainer` composable
- **Gesture-driven** — drag to curl, release to snap; velocity-aware fling detection
- **Tap navigation** — tap left/right thirds to turn pages; tap centre to invoke `onTap`
- **No external dependencies** — only Jetpack Compose and the Android NDK

## Requirements

| Item | Minimum |
|------|---------|
| Android | API 26 (Oreo) |
| NDK | 27.x |
| Compose BOM | 2024.x |

## Usage

```kotlin
// 1. Render your pages to Bitmaps (however you like)
val bitmaps: List<Bitmap> = pages.map { renderToBitmap(it) }

// 2. Drop in the composable
PageCurlContainer(
    pages           = bitmaps,
    backgroundColor = Color(0xFFF5F0E8),
    onPageChanged   = { current, total -> /* update progress */ },
    onReachEnd      = { /* load next chapter */ },
    onReachStart    = { /* load previous chapter */ },
    onTap           = { /* toggle UI controls */ }
)
```

## Algorithm

The curl effect uses a **cylindrical fold** computed in the vertex shader:

```
theta  = (x - foldX) / R          R ≈ 0.08 * pageWidth
new_x  = foldX + R * sin(theta)
new_z  = R * (1 - cos(theta))
```

Rendering passes (forward curl):
1. Next page — flat, full-page quad (bottom layer)
2. Reveal shadow — gradient strip at fold line
3. Current page — cylinder back face (mirrored UV, slightly lighter)
4. Current page — cylinder front face (darkens as theta → π/2)
5. Flat shadow — gradient strip on the still-flat portion

## Integration (composite build)

In your app's `settings.gradle.kts`:

```kotlin
includeBuild("../android-page-curl")
```

In your module's `build.gradle.kts`:

```kotlin
dependencies {
    implementation("io.github.readmigo:pagecurl")
}
```

## License

Apache 2.0
