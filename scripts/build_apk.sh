#!/usr/bin/env bash
# Builds bin/govnod-cube-debug.apk: compiles the native Vulkan cube with the
# NDK toolchain and packages a minimal NativeActivity APK without Gradle.
set -euo pipefail
set -x

SDK="${ANDROID_HOME:-/usr/local/lib/android/sdk}"

# Pick the newest installed NDK / build-tools / platform.
NDK_DIR="$(ls -d "$SDK"/ndk/* 2>/dev/null | sort -V | tail -1)"
if [ -z "$NDK_DIR" ]; then
    echo "ERROR: no NDK found under $SDK/ndk" >&2
    exit 1
fi
BUILD_TOOLS="$(ls -d "$SDK"/build-tools/* | sort -V | tail -1)"
PLATFORM_JAR="$(ls -d "$SDK"/platforms/android-* | sort -V | tail -1)/android.jar"
TARGET_SDK="$(basename "$(dirname "$PLATFORM_JAR")" | sed 's/^android-//')"
echo "NDK: $NDK_DIR"
echo "Build tools: $BUILD_TOOLS"
echo "Platform: $PLATFORM_JAR (targetSdk $TARGET_SDK)"

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
    -Ithirdparty/vulkan/include -Inative -Inative/shaders \
    -c native/gmcube.cpp -o bin/obj/gmcube.o

"$TOOLCHAIN/clang++" --target="$TRIPLE$API" -shared \
    -o bin/obj/libgmcube.so bin/obj/gmcube.o bin/obj/glue.o \
    -lvulkan -llog -landroid

# 2. Package the APK: link the manifest, add the native library, align, sign.
STAGE=bin/obj/apk
rm -rf "$STAGE"
mkdir -p "$STAGE/lib/arm64-v8a"
cp bin/obj/libgmcube.so "$STAGE/lib/arm64-v8a/"

"$BUILD_TOOLS/aapt2" link \
    -I "$PLATFORM_JAR" \
    --manifest app/AndroidManifest.xml \
    --min-sdk-version "$API" \
    --target-sdk-version "$TARGET_SDK" \
    -o bin/obj/base.apk

(cd "$STAGE" && zip -q -r ../base.apk lib)

"$BUILD_TOOLS/zipalign" -f -p 4 bin/obj/base.apk bin/obj/aligned.apk

keytool -genkeypair \
    -keystore bin/obj/debug.keystore \
    -storepass android -keypass android \
    -alias androiddebugkey \
    -dname "CN=Android Debug,O=Android,C=US" \
    -keyalg RSA -keysize 2048 -validity 10000

"$BUILD_TOOLS/apksigner" sign \
    --ks bin/obj/debug.keystore \
    --ks-pass pass:android --key-pass pass:android \
    --out bin/govnod-cube-debug.apk \
    bin/obj/aligned.apk

ls -la bin/
echo "OK: bin/govnod-cube-debug.apk"
