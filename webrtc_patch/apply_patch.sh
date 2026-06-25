#!/bin/bash
# Apply patched source files to WebRTC checkout.
# Run from the webrtc-checkout/src/ directory:
#   cd /path/to/webrtc-checkout/src
#   bash /path/to/this/apply_patch.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WEBRTC_SRC="${WEBRTC_SRC:-$(pwd)}"

echo "=== WebRTC patch installer ==="
echo "Source dir: $WEBRTC_SRC"
echo "Patch dir:  $SCRIPT_DIR"
echo ""

# Verify we're in a WebRTC source directory.
if [ ! -d "$WEBRTC_SRC/modules/rtp_rtcp" ]; then
  echo "ERROR: $WEBRTC_SRC does not look like a WebRTC src/ directory."
  echo "Run: cd /path/to/webrtc-checkout/src && bash $0"
  exit 1
fi

# Core WebRTC files (webrtc-checkout/src/ relative paths).
echo "Patching WebRTC core..."
cp -fv "$SCRIPT_DIR/media/engine/webrtc_video_engine.cc" \
  "$WEBRTC_SRC/media/engine/webrtc_video_engine.cc"
cp -fv "$SCRIPT_DIR/modules/rtp_rtcp/source/rtp_sender_video.cc" \
  "$WEBRTC_SRC/modules/rtp_rtcp/source/rtp_sender_video.cc"
cp -fv "$SCRIPT_DIR/modules/rtp_rtcp/source/rtp_sender.cc" \
  "$WEBRTC_SRC/modules/rtp_rtcp/source/rtp_sender.cc"
cp -fv "$SCRIPT_DIR/modules/rtp_rtcp/source/rtp_header_extension_map.cc" \
  "$WEBRTC_SRC/modules/rtp_rtcp/source/rtp_header_extension_map.cc"
cp -fv "$SCRIPT_DIR/modules/rtp_rtcp/source/rtp_header_extensions.h" \
  "$WEBRTC_SRC/modules/rtp_rtcp/source/rtp_header_extensions.h"
cp -fv "$SCRIPT_DIR/modules/rtp_rtcp/source/rtp_video_header.h" \
  "$WEBRTC_SRC/modules/rtp_rtcp/source/rtp_video_header.h"
cp -fv "$SCRIPT_DIR/modules/rtp_rtcp/include/rtp_rtcp_defines.h" \
  "$WEBRTC_SRC/modules/rtp_rtcp/include/rtp_rtcp_defines.h"
cp -fv "$SCRIPT_DIR/api/rtp_parameters.h" \
  "$WEBRTC_SRC/api/rtp_parameters.h"
cp -fv "$SCRIPT_DIR/api/rtp_parameters.cc" \
  "$WEBRTC_SRC/api/rtp_parameters.cc"
cp -fv "$SCRIPT_DIR/api/rtp_packet_info.h" \
  "$WEBRTC_SRC/api/rtp_packet_info.h"
cp -fv "$SCRIPT_DIR/api/rtp_packet_infos.h" \
  "$WEBRTC_SRC/api/rtp_packet_infos.h"
cp -fv "$SCRIPT_DIR/video/rtp_video_stream_receiver2.cc" \
  "$WEBRTC_SRC/video/rtp_video_stream_receiver2.cc"

# Demo headless_common files.
echo "Patching headless_common demo..."
DEST="$WEBRTC_SRC/examples/peerconnection/headless_common"
mkdir -p "$DEST"
cp -fv "$SCRIPT_DIR/examples/peerconnection/headless_common/per_frame_loss_tracker.h" \
  "$DEST/per_frame_loss_tracker.h"
cp -fv "$SCRIPT_DIR/examples/peerconnection/headless_common/per_frame_loss_tracker.cc" \
  "$DEST/per_frame_loss_tracker.cc"
cp -fv "$SCRIPT_DIR/sdl_renderer.h" \
  "$DEST/sdl_renderer.h"
cp -fv "$SCRIPT_DIR/sdl_renderer.mm" \
  "$DEST/sdl_renderer.mm"
cp -fv "$SCRIPT_DIR/receiver_sink.h" \
  "$DEST/receiver_sink.h"
cp -fv "$SCRIPT_DIR/receiver_sink.cc" \
  "$DEST/receiver_sink.cc"

echo ""
echo "=== Patch applied. Now rebuild ==="
echo "  cd $WEBRTC_SRC"
echo "  gn gen out/webrtc_demo --args='target_os=\"mac\" target_cpu=\"arm64\" is_debug=false is_component_build=false' --root=\"//examples/peerconnection\""
echo "  ninja -C out/webrtc_demo webrtc_sender webrtc_receiver"
