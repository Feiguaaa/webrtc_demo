#!/bin/bash
# Loop over all .y4m files in a directory, running the sender for each.
# Usage: ./run_sender_loop.sh --server=IP --port=PORT --video_dir=/path/to/videos/

set -e

SERVER=""
PORT=""
VIDEO_DIR=""

for arg in "$@"; do
  case "$arg" in
    --server=*) SERVER="${arg#*=}" ;;
    --port=*) PORT="${arg#*=}" ;;
    --video_dir=*) VIDEO_DIR="${arg#*=}" ;;
  esac
done

if [ -z "$SERVER" ] || [ -z "$VIDEO_DIR" ]; then
  echo "Usage: $0 --server=IP --port=PORT --video_dir=/path/to/videos/"
  exit 1
fi

PORT="${PORT:-8080}"
SENDER="./out/webrtc_sender"

echo "=== Sender Loop ==="
echo "Server: $SERVER:$PORT"
echo "Video dir: $VIDEO_DIR"

while true; do
  for video in "$VIDEO_DIR"/*.y4m; do
    [ -f "$video" ] || continue
    echo ""
    echo ">>> Sending: $(basename "$video")"
    $SENDER --server="$SERVER" --port="$PORT" --video_file="$video"
    echo ">>> Finished: $(basename "$video"), next in 2s..."
    sleep 2
  done
  echo "=== All videos done, restarting cycle ==="
done
