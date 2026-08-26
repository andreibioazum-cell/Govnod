#!/usr/bin/env bash
# Builds the cube: compiles the native Vulkan cube with the NDK toolchain,
# packages bin/govnod-cube-debug.apk and leaves the library under the
# bin/libgodot.android.template_debug.arm64.so name that CI expects.
set -euo pipefail

# Mirror all output into build.log and emit annotations on failure so the
# error is visible from the job summary without opening the full log.
exec > >(tee build.log) 2>&1
on_error() {
    tail -40 build.log 2>/dev/null | grep -Ei "error|fail|not found|No such" | tail -8 | while IFS= read -r line; do
        echo "::error::$(echo "$line" | cut -c1-220)"
    done
    echo "::error::scripts/build_apk.sh failed — see the job log for the full trace"
}
trap on_error EXIT

set -x

SDK="${ANDROID_HOME:-/usr/local/lib/android/sdk}"

# Pick the newest installed NDK.
NDK_DIR="$(ls -d "$SDK"/ndk/* 2>/dev/null | sort -V | tail -1)"
if [ -z "$NDK_DIR" ]; then
    echo "ERROR: no NDK found under $SDK/ndk" >&2
    exit 1
fi
echo "NDK: $NDK_DIR"

API=24
TRIPLE=aarch64-linux-android
TOOLCHAIN="$NDK_DIR/toolchains/llvm/prebuilt/linux-x86_64/bin"
GLUE_C="$NDK_DIR/sources/android/native_app_glue/android_native_app_glue.c"

if [ ! -f "$GLUE_C" ]; then
    echo "ERROR: native_app_glue not found at $GLUE_C" >&2
    exit 1
fi

mkdir -p bin/obj

# 1. Compile the native activity glue (C) and the cube (C++).
"$TOOLCHAIN/clang" --target="$TRIPLE$API" -O2 -Wall -Wextra \
    -I"$NDK_DIR/sources/android/native_app_glue" \
    -c "$GLUE_C" -o bin/obj/glue.o

"$TOOLCHAIN/clang++" --target="$TRIPLE$API" -O2 -std=gnu++17 -Wall -Wextra -Werror \
    -fPIC -funwind-tables -fstack-protector-strong \
    -DVK_USE_PLATFORM_ANDROID_KHR \
    -I"$NDK_DIR/sources/android/native_app_glue" \
    -Ithirdparty/vulkan/include -Inative -Inative/shaders \
    -c native/gmcube.cpp -o bin/obj/gmcube.o

"$TOOLCHAIN/clang++" --target="$TRIPLE$API" -shared \
    -o bin/obj/libgmcube.so bin/obj/gmcube.o bin/obj/glue.o \
    -lvulkan -llog -landroid

# 2. Package the APK.
bash scripts/package_apk.sh bin/obj/libgmcube.so bin/govnod-cube-debug.apk

# 3. CI expects the native library under the engine's template name.
cp bin/obj/libgmcube.so bin/libgodot.android.template_debug.arm64.so

ls -la bin/
echo "OK: bin/govnod-cube-debug.apk"
