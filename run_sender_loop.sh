#!/bin/bash
# Loop over all .y4m files in a directory, running the sender for each.
# After each video, kills and restarts both sender and receiver.
# Usage: ./run_sender_loop.sh --server=IP --port=PORT --video_dir=/path/to/videos/ --receiver_cmd='CMD'

SERVER=""
PORT=""
VIDEO_DIR=""
RECEIVER_CMD=""
BURST_LOSS=""

for arg in "$@"; do
  case "$arg" in
    --server=*) SERVER="${arg#*=}" ;;
    --port=*) PORT="${arg#*=}" ;;
    --video_dir=*) VIDEO_DIR="${arg#*=}" ;;
    --receiver_cmd=*) RECEIVER_CMD="${arg#*=}" ;;
    --burst_loss=*) BURST_LOSS="${arg#*=}" ;;
  esac
done

if [ -z "$SERVER" ] || [ -z "$VIDEO_DIR" ]; then
  echo "Usage: $0 --server=IP --port=PORT --video_dir=/path/to/videos/ --receiver_cmd='CMD'"
  exit 1
fi

PORT="${PORT:-8080}"
SENDER="./out/webrtc_sender"
RECEIVER_PID=""
SENDER_PID=""

echo "=== Sender Loop ==="
echo "Server: $SERVER:$PORT"
echo "Video dir: $VIDEO_DIR"

launch_receiver() {
  if [ -n "$RECEIVER_CMD" ]; then
    local name="rx_$(date +%s%N)"
    echo ">>> Launching receiver (name=$name)..."
    echo ">>> RECEIVER_CMD=$RECEIVER_CMD"
    $RECEIVER_CMD --name="$name" --low_latency &
    RECEIVER_PID=$!
    echo ">>> Receiver PID=$RECEIVER_PID"
    # Wait for signaling connection.
    sleep 3
    if ! kill -0 "$RECEIVER_PID" 2>/dev/null; then
      echo ">>> ERROR: Receiver died on startup!"
      RECEIVER_PID=""
      return 1
    fi
  fi
}

teardown_receiver() {
  if [ -n "$RECEIVER_PID" ]; then
    echo ">>> Tearing down receiver (PID=$RECEIVER_PID)..."
    kill "$RECEIVER_PID" 2>/dev/null
    # Wait up to 5 seconds for clean exit, then force kill.
    local count=0
    while kill -0 "$RECEIVER_PID" 2>/dev/null && [ $count -lt 50 ]; do
      sleep 0.1
      count=$((count + 1))
    done
    kill -9 "$RECEIVER_PID" 2>/dev/null
    wait "$RECEIVER_PID" 2>/dev/null || true
    RECEIVER_PID=""
    sleep 1
  fi
}

kill_sender() {
  if [ -n "$SENDER_PID" ] && kill -0 "$SENDER_PID" 2>/dev/null; then
    kill "$SENDER_PID" 2>/dev/null
    wait "$SENDER_PID" 2>/dev/null || true
  fi
  SENDER_PID=""
}

trap 'kill_sender; teardown_receiver' EXIT

# Collect videos.
videos=()
for video in "$VIDEO_DIR"/*.y4m; do
  [ -f "$video" ] || continue
  videos+=("$video")
done

if [ ${#videos[@]} -eq 0 ]; then
  echo "No .y4m files found in $VIDEO_DIR"
  exit 1
fi

echo "Found ${#videos[@]} video(s)"

for i in "${!videos[@]}"; do
  video="${videos[$i]}"
  echo ""
  echo "=== Video $((i+1))/${#videos[@]}: $(basename "$video") ==="

  # Start a fresh receiver for this video.
  launch_receiver
  if [ -z "$RECEIVER_PID" ]; then
    echo ">>> Skipping video due to receiver failure."
    continue
  fi

  # Run sender with timeout (kills cleanly after N seconds).
  sender_args=("$SENDER" "--server=$SERVER" "--port=$PORT" "--video_file=$video" "--max_bitrate=0")
  if [ -n "$BURST_LOSS" ]; then
    sender_args+=("--burst_loss=$BURST_LOSS")
  fi
  "${sender_args[@]}" > /tmp/sender_log_$i.txt 2>&1 &
  SENDER_PID=$!
  echo ">>> Sender PID=$SENDER_PID"
  (sleep 30 && kill $SENDER_PID 2>/dev/null) &
  KILLER_PID=$!
  wait $SENDER_PID 2>/dev/null || true
  kill $KILLER_PID 2>/dev/null || true
  wait $KILLER_PID 2>/dev/null || true
  SENDER_PID=""

  echo ">>> Sender output:"
  cat /tmp/sender_log_$i.txt 2>/dev/null | head -5
  echo ">>> Finished: $(basename "$video")"

  # Tear down receiver so the next video gets a new CSV.
  teardown_receiver

  # Wait for signaling server to clean up old peer entries.
  if [ $i -lt $((${#videos[@]} - 1)) ]; then
    echo ">>> Waiting for signaling server cleanup..."
    sleep 5
  fi
done

echo "=== All videos done ==="
