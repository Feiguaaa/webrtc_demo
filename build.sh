#!/bin/bash
# Build webrtc_demo targets and copy executables to ./out/
# Source files are synced from ./src to ../webrtc-checkout/src before building.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WEBRTC_ROOT="$(cd "$SCRIPT_DIR/../webrtc-checkout/src" 2>/dev/null && pwd)"

if [ -z "$WEBRTC_ROOT" ]; then
  echo "Error: webrtc-checkout not found at $SCRIPT_DIR/../webrtc-checkout/"
  exit 1
fi

OUT_DIR="$WEBRTC_ROOT/out/webrtc_demo"
GN="$WEBRTC_ROOT/buildtools/mac/gn"
JOBS=$(sysctl -n hw.ncpu)

# Sync source files to webrtc checkout
echo "Syncing source files..."
mkdir -p "$WEBRTC_ROOT/examples/peerconnection/headless_common"
mkdir -p "$WEBRTC_ROOT/examples/peerconnection/webrtc_sender"
mkdir -p "$WEBRTC_ROOT/examples/peerconnection/webrtc_receiver"

cp -f "$SCRIPT_DIR/src/headless_common/"* "$WEBRTC_ROOT/examples/peerconnection/headless_common/"
cp -f "$SCRIPT_DIR/src/webrtc_sender/"* "$WEBRTC_ROOT/examples/peerconnection/webrtc_sender/"
cp -f "$SCRIPT_DIR/src/webrtc_receiver/"* "$WEBRTC_ROOT/examples/peerconnection/webrtc_receiver/"
cp -f "$SCRIPT_DIR/src/client/"* "$WEBRTC_ROOT/examples/peerconnection/client/"

# Gen build if needed
if [ ! -f "$OUT_DIR/build.ninja" ]; then
  echo "Generating build..."
  "$GN" gen "$OUT_DIR" --root="$WEBRTC_ROOT" \
    --args='target_os="mac" target_cpu="arm64" is_debug=false is_component_build=false rtc_include_tests=true rtc_build_examples=true'
fi

# Build targets
mkdir -p "$SCRIPT_DIR/out"

for target in peerconnection_server webrtc_receiver webrtc_sender; do
  echo "Building $target..."
  ninja -C "$OUT_DIR" "$target" -j "$JOBS"
  cp "$OUT_DIR/$target" "$SCRIPT_DIR/out/"
  echo "  -> $SCRIPT_DIR/out/$target"
done

echo "Done. Executables:"
ls -lh "$SCRIPT_DIR/out/"
