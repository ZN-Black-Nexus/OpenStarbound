#!/usr/bin/env bash
# Builds the OpenStarbound Android APK without Gradle.
# Prerequisite: android/build-deps.sh has been run.
#
#   android/build-apk.sh        # -> dist/android/openstarbound-<abi>-debug.apk
#
# Environment: ANDROID_HOME, ANDROID_NDK_HOME, BUILD_TOOLS, PLATFORM, ABI, API, JOBS
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$HERE")"
SDK="${ANDROID_HOME:-$HOME/Android}"
NDK="${ANDROID_NDK_HOME:-$SDK/ndk/28.2.13676358}"
BT="${BUILD_TOOLS:-$SDK/build-tools/35.0.0}"
PLATFORM_JAR="$SDK/platforms/${PLATFORM:-android-34}/android.jar"
ABI="${ABI:-arm64-v8a}"
API="${API:-26}"
JOBS="${JOBS:-$(nproc)}"
KEYSTORE="${KEYSTORE:-$HOME/.android/debug.keystore}"

PREFIX="$HERE/.deps/prefix/$ABI"
SDL_JAVA="$HERE/.deps/src/sdl/android-project/app/src/main/java"
BUILD="$ROOT/build/android-$ABI"
STAGE="$ROOT/build/apk-$ABI"
OUT="$ROOT/dist/android"

[ -d "$PREFIX" ] || { echo "run android/build-deps.sh first" >&2; exit 1; }
[ -d "$SDL_JAVA" ] || { echo "SDL Java sources missing at $SDL_JAVA" >&2; exit 1; }
[ -f "$PLATFORM_JAR" ] || { echo "missing $PLATFORM_JAR" >&2; exit 1; }
mkdir -p "$OUT" "$BUILD"
rm -rf "$STAGE"; mkdir -p "$STAGE"/{classes,dex,res}

echo "== native game (libmain.so)"
cmake -S "$ROOT/source" -B "$BUILD" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI="$ABI" -DANDROID_PLATFORM="android-$API" -DANDROID_STL=c++_shared \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$PREFIX" -DCMAKE_FIND_ROOT_PATH="$PREFIX"
cmake --build "$BUILD" --target main -j "$JOBS"
LIBMAIN="$OUT/$ABI/libmain.so"
[ -f "$LIBMAIN" ] || LIBMAIN="$ROOT/dist/android/$ABI/libmain.so"
[ -f "$LIBMAIN" ] || { echo "libmain.so not found" >&2; exit 1; }

echo "== java"
# SDL's Java layer plus ours
javac -source 8 -target 8 -Xlint:-options -d "$STAGE/classes" -classpath "$PLATFORM_JAR" \
  $(find "$SDL_JAVA" "$HERE/app/java" -name '*.java')
"$BT/d8" --release --min-api "$API" --lib "$PLATFORM_JAR" --output "$STAGE/dex" \
  $(find "$STAGE/classes" -name '*.class')

echo "== resources"
"$BT/aapt2" compile --dir "$HERE/app/res" -o "$STAGE/res.zip"
"$BT/aapt2" link -o "$STAGE/base.apk" -I "$PLATFORM_JAR" --manifest "$HERE/app/AndroidManifest.xml" \
  --min-sdk-version "$API" --target-sdk-version 34 --version-code 1 --version-name "0.1.0" \
  "$STAGE/res.zip"

echo "== assembling"
# Bundled opensb assets: list + stamp so the app can extract them on first run
# (python is used instead of aapt because aapt skips files starting with '_' or '.').
python3 - "$ROOT" "$STAGE/base.apk" "$STAGE/dex/classes.dex" "$LIBMAIN" "$PREFIX" "$NDK" "$ABI" <<'PY'
import hashlib, os, sys, zipfile
root, apk, dex, libmain, prefix, ndk, abi = sys.argv[1:]
assets = os.path.join(root, "assets", "opensb")
files = []
for d, _, names in os.walk(assets):
    for n in names:
        files.append(os.path.relpath(os.path.join(d, n), assets).replace(os.sep, "/"))
files.sort()
h = hashlib.sha256()
for f in files:
    h.update(f.encode()); h.update(open(os.path.join(assets, f), "rb").read())
triple = {"arm64-v8a": "aarch64-linux-android", "x86_64": "x86_64-linux-android"}[abi]
libcxx = os.path.join(ndk, "toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib", triple, "libc++_shared.so")
libsdl = os.path.join(prefix, "lib", "libSDL3.so")
with zipfile.ZipFile(apk, "a", zipfile.ZIP_DEFLATED) as z:
    z.write(dex, "classes.dex")
    z.write(libmain, f"lib/{abi}/libmain.so")
    z.write(libsdl, f"lib/{abi}/libSDL3.so")
    z.write(libcxx, f"lib/{abi}/libc++_shared.so")
    z.writestr("assets/opensb.list", "\n".join(files) + "\n")
    z.writestr("assets/opensb.stamp", h.hexdigest())
    for f in files:
        z.write(os.path.join(assets, f), "assets/opensb/" + f)
PY

"$BT/zipalign" -f -p 4 "$STAGE/base.apk" "$STAGE/aligned.apk"
APK="$OUT/openstarbound-$ABI-debug.apk"
"$BT/apksigner" sign --ks "$KEYSTORE" --ks-pass pass:android --key-pass pass:android \
  --out "$APK" "$STAGE/aligned.apk"
"$BT/apksigner" verify "$APK"
echo "Built $APK"
