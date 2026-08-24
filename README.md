# Govnod

Mobile-only fork of the Godot Engine, stripped down for Android development.

## What's different from upstream Godot

- **Android only** — all other platforms (Windows, Linux, macOS, iOS, Web, visionOS) removed.
- **Mobile renderer only** — Forward+ and Compatibility (GLES3/OpenGL) renderers removed, Vulkan driver only.
- **English + Russian UI** — all other editor translations removed.
- **No donate / about screens** — the Donate button, "Support Godot Development" item, About/Credits windows and author/donor lists removed.
- Lots of unused platform code, drivers, modules and CI removed.

## Building (Android)

Requires Android SDK + NDK (see `platform/android/README.md`).

```
scons platform=android target=template_release arch=arm64
```

The editor can also be built for Android with `target=editor`.

Based on Godot Engine (https://godotengine.org), MIT license — see [LICENSE.txt](LICENSE.txt) and [COPYRIGHT.txt](COPYRIGHT.txt) for third-party licenses.
