#!/usr/bin/env bash
set -euo pipefail

# Manual APK build — no Gradle daemon, works on this aarch64 box.
# Host clang cross-compiles the root helper with the NDK sysroot.
# aapt2/zipalign are x86_64 and run through the local qemu shim.

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/android"
OUT="$ROOT/build"

NDK=/opt/sdk/ndk/25.2.9519653/toolchains/llvm/prebuilt/linux-x86_64
SYSROOT="$NDK/sysroot"
RES_DIR="$NDK/lib64/clang/14.0.7"
BT=/opt/sdk/build-tools/34.0.0
AJAR=/opt/sdk/platforms/android-34/android.jar
AAPT2=/opt/aapt2-shim/aapt2

CLANG=${CLANG:-clang}
QEMU=(qemu-x86_64 -L /opt/x86libs)

rm -rf "$OUT"
mkdir -p "$OUT/gen" "$OUT/classes" "$OUT/stage/assets"

echo "[1/6] root helper (arm64)"
"$CLANG" --target=aarch64-linux-android33 --sysroot="$SYSROOT" \
  -resource-dir="$RES_DIR" -fuse-ld=lld -static -Os -ffunction-sections \
  -Wl,--gc-sections -o "$OUT/esphelper" "$SRC/helper/helper.c" -lm

"$CLANG" --target=aarch64-linux-android33 --sysroot="$SYSROOT" \
  -resource-dir="$RES_DIR" -fuse-ld=lld -static -Os -ffunction-sections \
  -Wl,--gc-sections -o "$OUT/espdiscover" "$SRC/helper/discover.c" -lm

echo "[2/6] manifest / resources"
"$AAPT2" link -o "$OUT/base.apk" -I "$AJAR" \
  --manifest "$SRC/AndroidManifest.xml" --java "$OUT/gen" \
  --min-sdk-version 26 --target-sdk-version 34 --auto-add-overlay

echo "[3/6] java"
mapfile -t JAVAS < <(find "$SRC/src" "$OUT/gen" -name '*.java' 2>/dev/null)
javac -nowarn -source 8 -target 8 -Xlint:-options \
  -bootclasspath "$AJAR" -cp "$AJAR" -d "$OUT/classes" "${JAVAS[@]}"

echo "[4/6] dex"
mapfile -t CLASSES < <(find "$OUT/classes" -name '*.class')
java -cp "$BT/lib/d8.jar" com.android.tools.r8.D8 \
  --lib "$AJAR" --min-api 26 --output "$OUT/classes" "${CLASSES[@]}"

echo "[5/6] stage"
cp "$OUT/base.apk" "$OUT/unsigned.apk"
cp "$OUT/esphelper" "$OUT/stage/assets/esphelper"
cp "$OUT/espdiscover" "$OUT/stage/assets/espdiscover"
cp "$SRC/assets/config.txt" "$OUT/stage/assets/config.txt"
( cd "$OUT/classes" && zip -q -r "$OUT/unsigned.apk" classes.dex )
( cd "$OUT/stage" && zip -q -r "$OUT/unsigned.apk" assets )

echo "[6/6] align + sign"
"${QEMU[@]}" "$BT/zipalign" -f -p 4 "$OUT/unsigned.apk" "$OUT/aligned.apk"

if [[ ! -f "$OUT/debug.jks" ]]; then
  keytool -genkeypair -keystore "$OUT/debug.jks" -storepass android \
    -keypass android -alias oll -dname "CN=OLL ESP" \
    -validity 10000 -keyalg RSA -keysize 2048 >/dev/null 2>&1
fi
java -jar "$BT/lib/apksigner.jar" sign \
  --ks "$OUT/debug.jks" --ks-pass pass:android --key-pass pass:android \
  --out "$OUT/OLL-ESP.apk" "$OUT/aligned.apk"

java -jar "$BT/lib/apksigner.jar" verify --print-certs "$OUT/OLL-ESP.apk" | head -6
ls -lh "$OUT/OLL-ESP.apk"
