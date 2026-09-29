#!/bin/sh
# Infra Arcana -> WASM (Emscripten SDL2/SDL2_image/SDL2_mixer ports) + the RVIP page
# usage: sh web/build.sh [asan]
set -e
cd "$(dirname "$0")/.."
ROOT=$(pwd)
OBJ=${OBJ:-$ROOT/web/obj}
DIST=$ROOT/web/dist
JOBS=$(sysctl -n hw.ncpu 2>/dev/null || nproc)
CXXFLAGS="-O2 -DNDEBUG -std=c++17 -fexceptions -Iinclude -Ithird_party/mINI-0.9.17/src/mini -Ithird_party/tinyxml2 -sUSE_SDL=2 -sUSE_SDL_IMAGE=2 -sSDL2_IMAGE_FORMATS=[png] -sUSE_SDL_MIXER=2 -sSDL2_MIXER_FORMATS=[ogg] -Wno-deprecated-declarations"
LDFLAGS="-sASYNCIFY -sASYNCIFY_STACK_SIZE=65536 -sSTACK_SIZE=1048576 -sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=128MB -fexceptions -sEXPORTED_RUNTIME_METHODS=FS,IDBFS,HEAPU8,ENV,addRunDependency,removeRunDependency -sFORCE_FILESYSTEM -lidbfs.js -sENVIRONMENT=web -sUSE_SDL=2 -sUSE_SDL_IMAGE=2 -sSDL2_IMAGE_FORMATS=[png] -sUSE_SDL_MIXER=2 -sSDL2_MIXER_FORMATS=[ogg]"
if [ "$1" = asan ]; then
  CXXFLAGS="$CXXFLAGS -fsanitize=address -g"; LDFLAGS="$LDFLAGS -fsanitize=address -sINITIAL_MEMORY=256MB -sMAXIMUM_MEMORY=1GB"
  OBJ=$ROOT/web/obj-asan; DIST=$ROOT/web/dist-asan
fi
mkdir -p "$OBJ" "$DIST"
SRCS=$( { grep -oE 'src/[a-z_0-9]+\.cpp' CMakeLists.txt; echo third_party/tinyxml2/tinyxml2.cpp; } | sort -u)
export CXXFLAGS OBJ
echo "$SRCS" | xargs -P "$JOBS" -n 1 sh -c 'o=$OBJ/$(echo "$0" | tr / _).o; [ "$o" -nt "$0" ] || em++ $CXXFLAGS -c "$0" -o "$o"'
em++ $LDFLAGS "$OBJ"/*.o -o "$DIST/ia.js" --preload-file installed_files@/
cp web/index.html web/page.js "$DIST/"
# list icons: the game's own tiles at their original size (the page masks them with the item colour)
rm -rf "$DIST/gfx" && mkdir -p "$DIST/gfx/tiles" && cp -R installed_files/gfx/tiles/20x20 "$DIST/gfx/tiles/"
# text fonts: the index page's fonts/ (served at ../fonts/ next to the games)
IDX=$ROOT/../roguelikes-index
if [ -d "$IDX/fonts" ]; then (cd "$IDX/fonts" && ls *.woff | sed 's/\.woff$//') | python3 -c 'import json,sys; print(json.dumps(sys.stdin.read().split()))' > "$DIST/fonts.json"; else echo "[]" > "$DIST/fonts.json"; fi
