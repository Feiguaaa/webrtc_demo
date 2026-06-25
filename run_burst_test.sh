#!/bin/bash
# Quick test: 18s transmission with burst loss at 15s, 20% for 0.5s
# Receiver uses --low_latency to disable NACK/RTX retransmissions.
set -e

cd "$(dirname "$0")"

SERVER="./out/peerconnection_server"
SENDER="./out/webrtc_sender"
RECEIVER="./out/webrtc_receiver"

PORT=8080
VIDEO="/Users/jinx/Public/codespace/webrtc_demo/bbb_sunflower_1080p_30fps_normal.y4m"

# Clean old CSV
rm -f output/rtp_session_*.csv output/session_*.csv
mkdir -p output

echo "=== Starting test ==="

# 1. Start signaling server
$SERVER --port=$PORT &
SERVER_PID=$!
echo "Server PID=$SERVER_PID"
sleep 2

# 2. Start receiver (--low_latency strips NACK/RTX from SDP, no retransmissions)
$RECEIVER --server=127.0.0.1 --port=$PORT --play --low_latency --name="rx_test" > /tmp/receiver_burst_log.txt 2>&1 &
RECEIVER_PID=$!
echo "Receiver PID=$RECEIVER_PID"
sleep 8

if ! kill -0 "$RECEIVER_PID" 2>/dev/null; then
  echo "ERROR: Receiver died!"
  kill $SERVER_PID 2>/dev/null
  exit 1
fi

# 3. Start sender with burst loss
$SENDER --server=127.0.0.1 --port=$PORT --video_file="$VIDEO" --max_bitrate=0 \
  --burst_loss="5:0.5:0.2" &
SENDER_PID=$!
echo "Sender PID=$SENDER_PID"

# Wait 18 seconds then kill sender
(sleep 18 && kill $SENDER_PID 2>/dev/null) &
KILLER_PID=$!

wait $SENDER_PID 2>/dev/null || true
kill $KILLER_PID 2>/dev/null 2>/dev/null || true
wait $KILLER_PID 2>/dev/null || true

echo "=== Sender finished ==="
sleep 2

# 4. Kill receiver
kill $RECEIVER_PID 2>/dev/null || true
wait $RECEIVER_PID 2>/dev/null || true
sleep 1

# 5. Kill server
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true

echo "=== Test complete ==="
echo ""

# Show receiver SDP stripping status
echo "--- Receiver SDP status ---"
grep -E "nack|rtx" /tmp/receiver_burst_log.txt 2>/dev/null || echo "(no NACK/RTX info found)"
echo ""

# Show CSV files
echo "CSV files:"
ls -lh output/rtp_session_*.csv 2>/dev/null
echo ""

# Auto-plot
RTP_CSV=$(ls -t output/rtp_session_*.csv 2>/dev/null | head -1)
if [ -n "$RTP_CSV" ]; then
  echo "=== Plotting $RTP_CSV ==="
  python3 plot_burst_test.py "$RTP_CSV"
else
  echo "No rtp_session CSV found, skipping plot."
fi
