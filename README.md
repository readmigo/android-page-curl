<div align="center">

# android-page-curl

**Realistic OpenGL ES 3.0 page-curl for Jetpack Compose**

[![Build](https://github.com/readmigo/android-page-curl/actions/workflows/build.yml/badge.svg)](https://github.com/readmigo/android-page-curl/actions/workflows/build.yml)
[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![API](https://img.shields.io/badge/API-26%2B-brightgreen.svg)](https://android-arsenal.com/api?level=26)
[![Compose](https://img.shields.io/badge/Jetpack%20Compose-ready-4285F4.svg)](https://developer.android.com/jetpack/compose)
[![NDK](https://img.shields.io/badge/NDK-C%2B%2B17%20%7C%20OpenGL%20ES%203.0-orange.svg)](https://developer.android.com/ndk)
[![Release](https://img.shields.io/github/v/release/readmigo/android-page-curl?include_prereleases)](https://github.com/readmigo/android-page-curl/releases)

A pixel-perfect, hardware-accelerated page-curl animation library for Android, matching the visual quality of iOS `UIPageViewController(.pageCurl)`.

[Features](#features) · [Demo](#demo) · [Installation](#installation) · [Quick Start](#quick-start) · [API Reference](#api-reference) · [Algorithm](#algorithm-deep-dive) · [Architecture](#architecture) · [Contributing](#contributing)

</div>

---

## Demo

```
Forward curl (right → left)          Backward curl (left → right)

┌─────────────────────┐              ┌─────────────────────┐
│                  ╱  │              │  ╲                  │
│     PAGE N+1   ╱    │              │    ╲   PAGE N-1     │
│              ╱      │              │      ╲              │
│  ┌─────────╱┐       │              │       ┌╲───────────┐│
│  │PAGE N   ╲│       │              │       │╱   PAGE N  ││
│  │  (fold)  │       │              │       │   (fold)   ││
│  └──────────┘       │              │       └────────────┘│
└─────────────────────┘              └─────────────────────┘

     Drag ◀─────────                        ─────────▶ Drag
```

The page peels off the screen in a physically accurate cylinder, casting a shadow on the page underneath. Back-face of the turning page shows the reverse side (mirrored content, lighter tone).

---

## Features

| Feature | Details |
|---------|---------|
| **Cylindrical curl** | Computed per-vertex in a GLSL shader; R ≈ 8% of page width |
| **5-pass rendering** | Next page → reveal shadow → back face → front face → flat shadow |
| **Gesture-driven** | Drag to curl, release to snap forward or snap back |
| **Velocity-aware fling** | Threshold 500 dp/s; completes or cancels based on direction |
| **Tap navigation** | Left third ← previous, right third → next, center = `onTap` |
| **Spring animation** | `DampingRatioMediumBouncy` for natural snap feel |
| **Thread-safe bridge** | `@Volatile` state + `ConcurrentLinkedQueue` between Compose & GL |
| **Back-face rendering** | Mirrored UV, lighter shade — page looks like paper |
| **Drop shadow** | Gradient strip at fold line, fades with curl progress |
| **No extra deps** | Jetpack Compose + Android NDK only |
| **All 4 ABIs** | `arm64-v8a`, `armeabi-v7a`, `x86_64`, `x86` |

---

## Requirements

| Requirement | Version |
|-------------|---------|
| Android API | 26+ (Android 8.0 Oreo) |
| NDK | 27.x |
| Compose BOM | 2024.10.00+ |
| CMake | 3.22.1+ |
| AGP | 8.2.2+ |
| Kotlin | 1.9.22+ |
| C++ standard | C++17 |
| OpenGL ES | 3.0 |

---

## Installation

### Option A — Composite Build (local development / monorepo)

This is the fastest way to iterate — changes in the library are picked up immediately without publishing.

**1. Clone alongside your app:**
```
your-workspace/
├── MyApp/          ← your Android app
└── android-page-curl/   ← this library
```

**2. In `MyApp/settings.gradle.kts`:**
```kotlin
includeBuild("../android-page-curl") {
    dependencySubstitution {
        substitute(module("io.github.readmigo:pagecurl")).using(project(":pagecurl"))
    }
}
```

**3. In your module's `build.gradle.kts`:**
```kotlin
dependencies {
    implementation("io.github.readmigo:pagecurl")
}
```

### Option B — GitHub Releases (AAR)

Download the `.aar` from the [Releases page](https://github.com/readmigo/android-page-curl/releases) and add it as a local dependency:

```kotlin
// app/build.gradle.kts
dependencies {
    implementation(files("libs/pagecurl-1.x.y.aar"))
}
```

### Option C — GitHub Packages (Maven)

```kotlin
// settings.gradle.kts
dependencyResolutionManagement {
    repositories {
        maven {
            url = uri("https://maven.pkg.github.com/readmigo/android-page-curl")
            credentials {
                username = providers.gradleProperty("gpr.user").orNull
                    ?: System.getenv("GITHUB_ACTOR")
                password = providers.gradleProperty("gpr.key").orNull
                    ?: System.getenv("GITHUB_TOKEN")
            }
        }
    }
}

// build.gradle.kts
dependencies {
    implementation("io.github.readmigo:pagecurl:1.0.0")
}
```

---

## Quick Start

### 1. Render your content to Bitmaps

`PageCurlContainer` takes a `List<Bitmap>` — one per page. Render however suits your content:

```kotlin
// Example: render Compose content off-screen
fun renderPageToBitmap(width: Int, height: Int, content: @Composable () -> Unit): Bitmap {
    val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
    val canvas = Canvas(bitmap)
    // ... draw your content to canvas
    return bitmap
}

val pages: List<Bitmap> = myPageData.map { page ->
    renderPageToBitmap(screenWidth, screenHeight) {
        MyPageContent(page)
    }
}
```

### 2. Drop in the composable

```kotlin
@Composable
fun BookReader(pages: List<Bitmap>) {
    PageCurlContainer(
        pages           = pages,
        backgroundColor = Color(0xFFF5F0E8),   // warm paper color
        modifier        = Modifier.fillMaxSize(),
        onPageChanged   = { current, total ->
            println("Page $current of $total")
        },
        onReachEnd      = { /* load next chapter */ },
        onReachStart    = { /* load previous chapter */ },
        onTap           = { /* toggle toolbar visibility */ }
    )
}
```

### 3. Open at the last page (e.g., restore reading position)

```kotlin
PageCurlContainer(
    pages           = pages,
    startFromLastPage = true,   // opens at pages.last()
    onPageChanged   = { current, total -> savePosition(current) },
)
```

---

## API Reference

### `PageCurlContainer`

```kotlin
@Composable
fun PageCurlContainer(
    pages:             List<Bitmap>,
    backgroundColor:   Color   = Color.White,
    modifier:          Modifier = Modifier,
    startFromLastPage: Boolean  = false,
    onPageChanged:     (currentPage: Int, totalPages: Int) -> Unit = { _, _ -> },
    onReachStart:      () -> Unit = {},
    onReachEnd:        () -> Unit = {},
    onTap:             () -> Unit = {}
)
```

#### Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `pages` | `List<Bitmap>` | — | **Required.** Pre-rendered page bitmaps. All bitmaps should be the same size. |
| `backgroundColor` | `Color` | `Color.White` | Background color shown while textures upload, and between pages. |
| `modifier` | `Modifier` | `Modifier` | Standard Compose modifier. The view always fills all available space internally. |
| `startFromLastPage` | `Boolean` | `false` | If `true`, opens at `pages.last()` instead of `pages.first()`. |
| `onPageChanged` | `(Int, Int) -> Unit` | no-op | Called after every page turn, and once on initial composition. Arguments: 1-based `currentPage`, `totalPages`. |
| `onReachStart` | `() -> Unit` | no-op | Called when the user tries to go before page 1 (backward curl on first page). |
| `onReachEnd` | `() -> Unit` | no-op | Called when the user tries to go past the last page (forward curl on last page). |
| `onTap` | `() -> Unit` | no-op | Called when the user taps the **centre third** of the screen. |

#### Tap regions

```
┌────────┬────────────┬────────┐
│  ◀     │     ●      │     ▶  │
│ prev   │   onTap    │  next  │
│ page   │            │  page  │
│ (1/3)  │   (1/3)    │  (1/3) │
└────────┴────────────┴────────┘
```

#### Notes

- Bitmaps are uploaded to GL textures on the GL thread to avoid jank on the UI thread.
- The library keeps at most 3 textures loaded: current, next, previous.
- Changing the `pages` list triggers a re-upload of the relevant textures; you can update the list live (e.g., lazy loading).
- `onPageChanged` is invoked with the initial page on first composition.

---

## Algorithm Deep Dive

### Cylindrical Fold Model

A page curl is modelled as a paper sheet wrapping around an invisible cylinder of radius **R**.

```
Side view of page folding around cylinder (radius R):

  flat part         cylinder arc           (air)
──────────────── · · · ·    · ·  · ·
                            ↑ fold axis
                         foldX

theta = (x - foldX) / R

For each vertex at position x > foldX:
  new_x = foldX + R * sin(theta)     ← moves left as it curls up
  new_z = R * (1 - cos(theta))       ← lifts off the page surface
```

The radius **R = 0.08 × page_width** gives the appearance of stiff paper (small R = rigid, large R = floppy).

### GLSL Vertex Shader

```glsl
// Cylindrical transform
float theta = (aPosition.x - uFoldX) / uRadius;
float newX  = uFoldX + uRadius * sin(theta);
float newZ  = uRadius * (1.0 - cos(theta));

// Shadow darkening proportional to curl angle
vShadow = uDarken * sin(theta);
```

### 5-Pass Render Pipeline (forward curl)

```
  ┌─────────────────────────────────────────────────────────────┐
  │  Pass 1: Draw NEXT page flat (full quad, depth 0)           │
  │          → becomes visible as current page peels away       │
  ├─────────────────────────────────────────────────────────────┤
  │  Pass 2: Reveal shadow (gradient strip left of fold)        │
  │          → darkens the next page where fold line approaches │
  ├─────────────────────────────────────────────────────────────┤
  │  Pass 3: CURRENT page — cylinder BACK FACE                  │
  │          → UV mirrored horizontally, lighter tone           │
  │          → α-blended, glBlendFunc(ONE, ZERO)                │
  ├─────────────────────────────────────────────────────────────┤
  │  Pass 4: CURRENT page — cylinder FRONT FACE                 │
  │          → real UV, darkens as theta → π/2                  │
  │          → foldX mesh segment only                          │
  ├─────────────────────────────────────────────────────────────┤
  │  Pass 5: Flat shadow (gradient to the right of fold line)   │
  │          → cast shadow on still-flat portion of current pg  │
  └─────────────────────────────────────────────────────────────┘
```

Backward curl mirrors the same 5 passes with `mirrorFoldX = 1.0 - foldX`.

### Mesh Resolution

```
MESH_COLS = 64   ← horizontal subdivisions (fine enough for smooth curve)
MESH_ROWS = 2    ← vertical (uniform, no vertical curl)

Total vertices: (64+1) × (64+1) = 65 × 65 = 4 225
Total triangles: 64 × 64 × 2   = 8 192
```

---

## Architecture

### Component Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                     Jetpack Compose UI Thread                    │
│                                                                  │
│  PageCurlContainer (Composable)                                  │
│  ├── curlProgress: Animatable<Float>   (spring animation)        │
│  ├── curlForward: Boolean              (direction)               │
│  ├── currentPage: Int                  (current index)           │
│  ├── pointerInput { detectDragGestures }                         │
│  ├── pointerInput { detectTapGestures }                          │
│  └── snapshotFlow { curlProgress, curlForward }                  │
│             │                                                    │
│             │  writes @Volatile fields                           │
│             ▼                                                    │
│  PageCurlRenderState                                             │
│  ├── @Volatile foldX: Float                                      │
│  ├── @Volatile isForward: Boolean                                │
│  └── pendingTextures: ConcurrentLinkedQueue<Pair<Int, Bitmap>>   │
│                  │                                               │
└──────────────────│───────────────────────────────────────────────┘
                   │  reads on GL thread
┌──────────────────│───────────────────────────────────────────────┐
│                  ▼          GL Thread (GLSurfaceView)            │
│                                                                  │
│  PageCurlGLRenderer (GLSurfaceView.Renderer)                     │
│  ├── onSurfaceCreated  → nativeSurfaceCreated(ptr)               │
│  ├── onSurfaceChanged  → nativeSurfaceChanged(ptr, w, h)         │
│  └── onDrawFrame                                                 │
│      ├── drain pendingTextures → nativeSetBitmap(ptr, slot, bmp) │
│      └── nativeDrawForward(ptr, foldX)                           │
│          nativeDrawBackward(ptr, foldX)                          │
│                  │                                               │
└──────────────────│───────────────────────────────────────────────┘
                   │  JNI
┌──────────────────│───────────────────────────────────────────────┐
│                  ▼          C++ / libpagecurl.so                  │
│                                                                  │
│  PageCurlRenderer                                                │
│  ├── GLuint textures[3]   (current=0, next=1, prev=2)            │
│  ├── GLuint curlVAO/VBO/EBO   (cylinder mesh)                    │
│  ├── GLuint flatVAO/VBO/EBO   (full-screen quad)                 │
│  ├── GLuint curlProgram   (cylindrical GLSL)                     │
│  └── GLuint flatProgram   (passthrough GLSL)                     │
└─────────────────────────────────────────────────────────────────┘
```

### Threading Model

```
Compose thread               GL thread
─────────────────            ──────────────────────────────────
Animatable.animateTo()  →    snapshotFlow reads value
  sets curlProgress          writes renderState.foldX (@Volatile)
                             ─────────────────────────────────
LaunchedEffect(pages) →      onDrawFrame drains pendingTextures
  enqueues bitmaps           uploads to GL textures via JNI
  (ConcurrentLinkedQueue)
                             ─────────────────────────────────
DisposableEffect.onDispose   nativeDestroy(ptr)
  nativePtr.getAndSet(0L)    (called synchronously; safe because
                              GLSurfaceView is already paused)
```

### File Structure

```
android-page-curl/
├── pagecurl/
│   ├── build.gradle.kts
│   └── src/main/
│       ├── cpp/
│       │   ├── CMakeLists.txt          ← CMake build, links GLESv3 jnigraphics
│       │   ├── page_curl_renderer.h    ← C++ class declaration
│       │   ├── page_curl_renderer.cpp  ← OpenGL ES 3.0 render logic
│       │   └── page_curl_jni.cpp       ← JNI bridge (nativeCreate…nativeDestroy)
│       └── kotlin/io/github/readmigo/pagecurl/
│           ├── PageCurlJNI.kt          ← internal Kotlin JNI object
│           └── PageCurlContainer.kt    ← public @Composable entry point
├── gradle/
│   ├── libs.versions.toml
│   └── wrapper/
├── .github/
│   ├── workflows/
│   │   ├── build.yml       ← CI: build on every push/PR
│   │   └── release.yml     ← Release: build AAR + publish on git tag
│   └── ISSUE_TEMPLATE/
├── build.gradle.kts
├── settings.gradle.kts
├── gradle.properties
└── README.md
```

---

## Customization

### Change the curl radius

In `page_curl_renderer.h`, adjust `CURL_RADIUS`:

```cpp
// Smaller = stiffer paper (iOS-style)
static constexpr float CURL_RADIUS = 0.06f;

// Larger = more flexible / dramatic curl
static constexpr float CURL_RADIUS = 0.12f;
```

### Change mesh resolution

Higher `MESH_COLS` = smoother curve at the cost of more vertices:

```cpp
static constexpr int MESH_COLS = 128;   // ultra-smooth
static constexpr int MESH_ROWS = 4;     // add vertical subdivisions
```

### Change shadow intensity

In `page_curl_renderer.cpp`, the fragment shader multiplier:

```glsl
// 0.45 = 45% maximum darkening at theta = π/2
color.rgb *= (1.0 - vShadow * 0.45);
```

### Change animation spring

In `PageCurlContainer.kt`:

```kotlin
// More bouncy
curlProgress.animateTo(1f, spring(Spring.DampingRatioHighBouncy, Spring.StiffnessMedium))

// Crisp, no bounce
curlProgress.animateTo(1f, spring(Spring.DampingRatioNoBouncy, Spring.StiffnessHigh))
```

### Change gesture thresholds

```kotlin
// In PageCurlContainer.kt
private const val COMPLETION_THRESHOLD = 0.35f  // drag 35% of width to commit
private const val VELOCITY_THRESHOLD   = 500f   // dp/s fling threshold
```

---

## Performance Notes

- **GL textures** are uploaded on the GL thread (inside `onDrawFrame`) to avoid blocking the UI thread.
- Only **3 textures** are resident at any time (current, next, previous). A 1080p page at ARGB_8888 is ~8 MB; 3 pages = ~24 MB GPU memory.
- The **mesh** (4 225 vertices × 5 passes) is drawn with indexed triangles; one `glDrawElements` call per pass.
- `RENDERMODE_CONTINUOUSLY` ensures smooth animation during drags; there is no adaptive throttling currently.
- Consider pre-rendering bitmaps on a background thread before passing them to `PageCurlContainer`.

---

## Comparison

| | android-page-curl | Canvas-based wipe | ViewPager2 |
|---|---|---|---|
| Visual fidelity | ★★★★★ | ★★☆☆☆ | ★★★☆☆ |
| GPU accelerated | ✅ OpenGL ES 3.0 | ⚠️ HW Canvas | ✅ |
| Compose API | ✅ | ✅ | ⚠️ via interop |
| Page curl effect | ✅ cylindrical | ❌ | ❌ |
| Shadow | ✅ | ❌ | ❌ |
| Back face | ✅ | ❌ | ❌ |
| Dependencies | Compose + NDK | Compose only | Compose + ViewPager2 |
| Min API | 26 | 21 | 21 |

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for development setup, build instructions, and pull request guidelines.

Quick start:

```bash
# Clone with submodules (if needed)
git clone https://github.com/readmigo/android-page-curl.git

# Build the library AAR
./gradlew :pagecurl:assembleRelease

# Run checks
./gradlew :pagecurl:lint
```

---

## License

```
Copyright 2026 Readmigo

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
```
