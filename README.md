# Govnod Cube

The Govnod fork, taken to its logical conclusion: the engine is gone.

What remains is a single C++ file (`native/gmcube.cpp`) that renders a
spinning 3D cube on Android using Vulkan — no engine, no Gradle, no
dependencies beyond the NDK and the Vulkan headers.

- **Android only, arm64** — `minSdk 24`
- **Vulkan only** — `NativeActivity` + `VK_KHR_android_surface`
- **No Java code** — `android:hasCode="false"`, the APK is a manifest plus one `.so`
- Back button exits the app

## Build

CI builds it on every push (see `.github/workflows/main.yml`).
Locally, with the Android SDK + NDK installed:

```sh
ANDROID_HOME=$HOME/Android/Sdk bash scripts/build_apk.sh
# -> bin/govnod-cube-debug.apk
```

The build compiles `native/gmcube.cpp` and the NDK's `native_app_glue`
with the NDK clang, then packages and signs the APK with `aapt2`,
`zipalign` and `apksigner` — no Gradle involved.

## Shaders

`native/shaders/cube.vert` and `cube.frag` are compiled to SPIR-V with
`glslang`; the result is checked in as `native/shaders/shaders.h`.

## History

This repository used to be a mobile-only fork of the Godot Engine
(MIT license, https://godotengine.org). Then the engine was removed.
The vendored Vulkan headers in `thirdparty/vulkan/` remain from that era.

---

## По-русски

Движок убран. Остался один файл `native/gmcube.cpp`, который рисует
вращающийся 3D-куб на Vulkan. Никакого движка, никакого Gradle — только
NDK и Vulkan. CI на каждый пуш собирает `bin/govnod-cube-debug.apk`.
