#!/usr/bin/env bash
# Build the browser port: pc_port compiled to WebAssembly with Emscripten, then
# staged next to web_port/www/ so the folder in dist/ is the whole site.
# No game data is bundled; the page asks the player for their own disc image.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
EMSDK="${EMSDK:-C:/Claude/emsdk}"
BUILD="$HERE/build"
DIST="$HERE/dist"
CONFIG="${1:-Release}"

# shellcheck disable=SC1091
source "$EMSDK/emsdk_env.sh" >/dev/null 2>&1

GEN=()
if command -v ninja >/dev/null 2>&1; then
    GEN=(-G Ninja)
fi

emcmake cmake -S "$ROOT/pc_port" -B "$BUILD" "${GEN[@]}" \
    -DCMAKE_BUILD_TYPE="$CONFIG" \
    -DSH_ENABLE_RETROACHIEVEMENTS=OFF
cmake --build "$BUILD" --target SilentHillPC -j "$(nproc 2>/dev/null || echo 8)"

rm -rf "$DIST"
mkdir -p "$DIST"
cp -r "$HERE/www/." "$DIST/"
cp "$BUILD/SilentHillPC.js" "$BUILD/SilentHillPC.wasm" "$BUILD/SilentHillPC.data" "$DIST/"

echo
echo "Site staged in: $DIST"
ls -la "$DIST"
