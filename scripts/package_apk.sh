#!/usr/bin/env bash
# Packages a minimal NativeActivity APK around a given native library.
#
#   usage: package_apk.sh <path-to-libxxx.so> <output.apk>
#
# Uses aapt2 / zipalign / apksigner from the Android SDK build-tools.
set -euo pipefail

SO_PATH="${1:?usage: package_apk.sh <lib.so> <out.apk>}"
OUT_APK="${2:?usage: package_apk.sh <lib.so> <out.apk>}"

SDK="${ANDROID_HOME:-/usr/local/lib/android/sdk}"
BUILD_TOOLS="$(ls -d "$SDK"/build-tools/* | sort -V | tail -1)"
PLATFORM_JAR="$(ls -d "$SDK"/platforms/android-* | sort -V | tail -1)/android.jar"
TARGET_SDK="$(basename "$(dirname "$PLATFORM_JAR")" | sed 's/^android-//')"
API=24

STAGE="$(dirname "$OUT_APK")/obj/apk-stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/lib/arm64-v8a" "$(dirname "$OUT_APK")"
cp "$SO_PATH" "$STAGE/lib/arm64-v8a/$(basename "$SO_PATH")"

"$BUILD_TOOLS/aapt2" link \
    -I "$PLATFORM_JAR" \
    --manifest app/AndroidManifest.xml \
    --min-sdk-version "$API" \
    --target-sdk-version "$TARGET_SDK" \
    -o "$STAGE/base.apk"

(cd "$STAGE" && zip -q -r base.apk lib)

"$BUILD_TOOLS/zipalign" -f -p 4 "$STAGE/base.apk" "$STAGE/aligned.apk"

KEYSTORE="$STAGE/debug.keystore"
keytool -genkeypair \
    -keystore "$KEYSTORE" \
    -storepass android -keypass android \
    -alias androiddebugkey \
    -dname "CN=Android Debug,O=Android,C=US" \
    -keyalg RSA -keysize 2048 -validity 10000

"$BUILD_TOOLS/apksigner" sign \
    --ks "$KEYSTORE" \
    --ks-pass pass:android --key-pass pass:android \
    --out "$OUT_APK" \
    "$STAGE/aligned.apk"

echo "OK: $OUT_APK"
