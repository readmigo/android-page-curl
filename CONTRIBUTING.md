# Contributing to android-page-curl

Thank you for your interest in contributing! This document covers everything you need to get started.

## Table of Contents

- [Development Environment](#development-environment)
- [Project Structure](#project-structure)
- [Building the Library](#building-the-library)
- [Running Tests](#running-tests)
- [Making Changes](#making-changes)
- [Submitting a Pull Request](#submitting-a-pull-request)
- [Code Style](#code-style)
- [Reporting Bugs](#reporting-bugs)
- [Requesting Features](#requesting-features)

---

## Development Environment

**Required tools:**

| Tool | Version |
|------|---------|
| Android Studio | Ladybug (2024.2.1) or newer |
| JDK | 17 |
| NDK | 27.0.12077973 (install via SDK Manager → SDK Tools → NDK) |
| CMake | 3.22.1+ (install via SDK Manager → SDK Tools → CMake) |
| Git | 2.x |

**Clone the repository:**

```bash
git clone https://github.com/readmigo/android-page-curl.git
cd android-page-curl
```

**Open in Android Studio:**

File → Open → select the `android-page-curl/` folder. Android Studio will detect the Gradle project and prompt to sync.

---

## Project Structure

```
android-page-curl/
├── pagecurl/                   ← the library module
│   ├── build.gradle.kts
│   └── src/main/
│       ├── cpp/
│       │   ├── CMakeLists.txt          ← CMake configuration
│       │   ├── page_curl_renderer.h    ← Renderer class header
│       │   ├── page_curl_renderer.cpp  ← OpenGL ES 3.0 rendering
│       │   └── page_curl_jni.cpp       ← JNI bridge
│       └── kotlin/io/github/readmigo/pagecurl/
│           ├── PageCurlJNI.kt          ← Kotlin JNI declarations (internal)
│           └── PageCurlContainer.kt    ← Public Composable API
├── gradle/libs.versions.toml   ← Version catalog
├── build.gradle.kts            ← Root build (declares group)
├── settings.gradle.kts
└── gradle.properties
```

---

## Building the Library

**Build the AAR (release):**

```bash
./gradlew :pagecurl:assembleRelease
# Output: pagecurl/build/outputs/aar/pagecurl-release.aar
```

**Build all variants:**

```bash
./gradlew :pagecurl:assemble
```

**Build for a specific ABI only (faster during development):**

In `pagecurl/build.gradle.kts`, temporarily restrict `abiFilters`:

```kotlin
ndk {
    abiFilters += listOf("arm64-v8a")   // change while developing
}
```

**Run lint:**

```bash
./gradlew :pagecurl:lint
# Report: pagecurl/build/reports/lint-results-debug.html
```

---

## Running Tests

The library currently has no instrumented tests (all logic is in C++ / GL). Contributions adding tests are welcome.

To run lint and static checks:

```bash
./gradlew :pagecurl:check
```

---

## Making Changes

### C++ / OpenGL changes (`page_curl_renderer.cpp`)

- Keep the GLSL shaders as raw string literals in the `.cpp` file for easy editing.
- The renderer is designed to be stateless across frames (all state lives in uniform variables). Maintain this pattern.
- Shader compilation errors are logged via Android `__android_log_print`. Check `adb logcat -s pagecurl` for GL errors.
- When adding new uniform variables, add a `GLint` field to the renderer class and look it up in the shader initialization path.

### Kotlin / Compose changes (`PageCurlContainer.kt`)

- The `PageCurlRenderState` bridges the Compose thread and the GL thread. Any new state that GL needs must go through this class using `@Volatile` for primitive values or `ConcurrentLinkedQueue` for reference types.
- Do not call JNI functions from the Compose/UI thread — all JNI calls must happen in `onDrawFrame` (GL thread).
- The `nativePtr` `AtomicLong` is the authoritative handle. Always check `ptr != 0L` before any JNI call.

### Adding new public API

- Keep the public API minimal and Compose-idiomatic.
- Parameters should have sensible defaults.
- Add KDoc to any new public symbols.
- Update `README.md` API Reference accordingly.

---

## Submitting a Pull Request

1. **Fork** the repository and create your branch from `main`:
   ```bash
   git checkout -b feature/my-improvement
   ```

2. **Make your changes** following the code style guidelines below.

3. **Build and verify** the library compiles cleanly:
   ```bash
   ./gradlew :pagecurl:assembleRelease :pagecurl:lint
   ```

4. **Commit** with a descriptive message:
   ```bash
   git commit -m "feat: add page shadow intensity parameter"
   ```

5. **Push** and open a Pull Request against `main`.

6. Fill out the PR template, describing what changed and why.

### Commit message format

Follow [Conventional Commits](https://www.conventionalcommits.org/):

```
feat: add startFromLastPage parameter
fix: correct UV flip on back-face rendering
docs: expand algorithm section in README
perf: reduce vertex count for MESH_ROWS < 4
refactor: extract shadow pass into helper method
chore: bump NDK to 27.1
```

---

## Code Style

### Kotlin

- Follow the [Kotlin coding conventions](https://kotlinlang.org/docs/coding-conventions.html).
- 4-space indentation.
- No wildcard imports.
- `internal` for anything not part of the public API.

### C++

- C++17.
- 4-space indentation.
- Use `nullptr`, not `NULL` or `0` for pointer null checks.
- Prefer `constexpr` over `#define` for constants.
- Log errors with `LOGE` (defined as `__android_log_print(ANDROID_LOG_ERROR, "pagecurl", ...)`) and return gracefully — never crash.
- Free all GL resources in `release()`.

---

## Reporting Bugs

Use the [Bug Report template](.github/ISSUE_TEMPLATE/bug_report.yml).

Please include:
- Android version and device model
- NDK and AGP versions
- A minimal reproducible example or steps to reproduce
- Logcat output (filter: `adb logcat -s pagecurl`)

---

## Requesting Features

Use the [Feature Request template](.github/ISSUE_TEMPLATE/feature_request.yml).

Describe:
- The use case you're trying to solve
- What the API might look like
- Any alternatives you've considered
