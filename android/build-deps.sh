#!/usr/bin/env bash
# Cross-compiles every native dependency OpenStarbound needs for Android
# (arm64-v8a by default) from pinned upstream sources. No vcpkg involved.
#
#   android/build-deps.sh            # builds into android/.deps/prefix/<abi>
#
# Environment:
#   ANDROID_NDK_HOME  NDK root (default: ~/Android/ndk/28.2.13676358)
#   ABI               arm64-v8a (default) | x86_64 (emulator)
#   API               minimum Android API level (default 26)
#   JOBS              parallel build jobs
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NDK="${ANDROID_NDK_HOME:-$HOME/Android/ndk/28.2.13676358}"
ABI="${ABI:-arm64-v8a}"
API="${API:-26}"
JOBS="${JOBS:-$(nproc)}"

DEPS="$HERE/.deps"
SRC="$DEPS/src"
BUILD="$DEPS/build/$ABI"
PREFIX="$DEPS/prefix/$ABI"
mkdir -p "$SRC" "$BUILD" "$PREFIX"

TOOLCHAIN="$NDK/build/cmake/android.toolchain.cmake"
[ -f "$TOOLCHAIN" ] || { echo "NDK toolchain not found at $TOOLCHAIN" >&2; exit 1; }

# name  git-url  tag
SDL_REPO=https://github.com/libsdl-org/SDL.git;              SDL_TAG=release-3.4.20
OGG_REPO=https://github.com/xiph/ogg.git;                    OGG_TAG=v1.3.6
VORBIS_REPO=https://github.com/xiph/vorbis.git;              VORBIS_TAG=v1.3.7
ZLIBNG_REPO=https://github.com/zlib-ng/zlib-ng.git;          ZLIBNG_TAG=2.3.3
PNG_REPO=https://github.com/pnggroup/libpng.git;             PNG_TAG=v1.6.59
FT_REPO=https://github.com/freetype/freetype.git;            FT_TAG=VER-2-14-3
OPUS_REPO=https://github.com/xiph/opus.git;                  OPUS_TAG=v1.6.1
ZSTD_REPO=https://github.com/facebook/zstd.git;              ZSTD_TAG=v1.5.7
ABSL_REPO=https://github.com/abseil/abseil-cpp.git;          ABSL_TAG=20260107.1
RE2_REPO=https://github.com/google/re2.git;                  RE2_TAG=2025-11-05
IMGUI_REPO=https://github.com/ocornut/imgui.git;             IMGUI_TAG=v1.92.0
MIMALLOC_REPO=https://github.com/microsoft/mimalloc.git;     MIMALLOC_TAG=v3.6.1

fetch() { # name repo tag
  local dir="$SRC/$1"
  if [ ! -d "$dir/.git" ]; then
    git clone --depth 1 --branch "$3" "$2" "$dir"
  fi
}

COMMON=(
  -G Ninja
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN"
  -DANDROID_ABI="$ABI"
  -DANDROID_PLATFORM="android-$API"
  -DANDROID_STL=c++_shared
  -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_INSTALL_PREFIX="$PREFIX"
  -DCMAKE_PREFIX_PATH="$PREFIX"
  -DCMAKE_FIND_ROOT_PATH="$PREFIX"
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON
  -DBUILD_SHARED_LIBS=OFF
  -DBUILD_TESTING=OFF
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5
)

build() { # name srcsubdir [extra cmake args...]
  local name="$1" sub="$2"; shift 2
  if [ -f "$BUILD/$name.done" ]; then echo "== $name (cached)"; return; fi
  echo "== building $name"
  cmake -S "$SRC/$name/$sub" -B "$BUILD/$name" "${COMMON[@]}" "$@"
  cmake --build "$BUILD/$name" -j"$JOBS"
  cmake --install "$BUILD/$name"
  touch "$BUILD/$name.done"
}

fetch sdl      "$SDL_REPO"      "$SDL_TAG"
fetch ogg      "$OGG_REPO"      "$OGG_TAG"
fetch vorbis   "$VORBIS_REPO"   "$VORBIS_TAG"
fetch zlibng   "$ZLIBNG_REPO"   "$ZLIBNG_TAG"
fetch png      "$PNG_REPO"      "$PNG_TAG"
fetch freetype "$FT_REPO"       "$FT_TAG"
fetch opus     "$OPUS_REPO"     "$OPUS_TAG"
fetch zstd     "$ZSTD_REPO"     "$ZSTD_TAG"
fetch absl     "$ABSL_REPO"     "$ABSL_TAG"
fetch re2      "$RE2_REPO"      "$RE2_TAG"
fetch imgui    "$IMGUI_REPO"    "$IMGUI_TAG"
fetch mimalloc "$MIMALLOC_REPO" "$MIMALLOC_TAG"

# SDL3 is built as a shared library: SDLActivity loads libSDL3.so, then libmain.so.
build sdl . -DBUILD_SHARED_LIBS=ON -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF \
  -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF

build ogg . -DINSTALL_DOCS=OFF
build vorbis . -DOGG_ROOT="$PREFIX"

build zlibng . -DZLIB_COMPAT=OFF -DWITH_GZFILEOP=ON -DZLIB_ENABLE_TESTS=OFF -DWITH_GTEST=OFF

# libpng and freetype use the zlib that ships in the NDK sysroot.
build png . -DPNG_SHARED=OFF -DPNG_STATIC=ON -DPNG_TESTS=OFF -DPNG_TOOLS=OFF -DPNG_FRAMEWORK=OFF \
  -DPNG_HARDWARE_OPTIMIZATIONS=ON

build freetype . -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_BROTLI=ON -DFT_DISABLE_HARFBUZZ=ON \
  -DFT_DISABLE_PNG=ON

build opus . -DOPUS_BUILD_PROGRAMS=OFF -DOPUS_BUILD_TESTING=OFF -DOPUS_INSTALL_PKG_CONFIG_MODULE=OFF

build zstd build/cmake -DZSTD_BUILD_PROGRAMS=OFF -DZSTD_BUILD_SHARED=OFF -DZSTD_BUILD_STATIC=ON \
  -DZSTD_BUILD_TESTS=OFF -DZSTD_BUILD_CONTRIB=OFF

build absl . -DABSL_PROPAGATE_CXX_STD=ON -DABSL_BUILD_TESTING=OFF -DCMAKE_CXX_STANDARD=17 \
  -DABSL_ENABLE_INSTALL=ON

build re2 . -DRE2_BUILD_TESTING=OFF -DCMAKE_CXX_STANDARD=17

build mimalloc . -DMI_BUILD_SHARED=OFF -DMI_BUILD_TESTS=OFF -DMI_BUILD_OBJECT=OFF

echo "Dependencies installed to $PREFIX"
echo "imgui sources: $SRC/imgui"
