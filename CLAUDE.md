# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a **WebRTC RTP-layer per-frame packet loss tracking** demo built on top of the WebRTC native codebase. It instruments the RTP pipeline to track which packets belong to which video frames and records per-frame loss data to CSV — even for completely lost frames.

The project lives as a standalone demo directory (`webrtc_demo/`) that syncs source files into a WebRTC checkout (`../webrtc-checkout/src/examples/peerconnection/`) before building.

## Build Instructions

```bash
# Build directory (in the WebRTC checkout)
BUILD_DIR=/Users/jinx/Public/codespace/webrtc-checkout/src/out/webrtc_demo

# Build all targets (syncs src/ → webrtc-checkout, then compiles)
./build.sh

# Or build individual targets manually:
ninja -C $BUILD_DIR webrtc_sender webrtc_receiver peerconnection_server
```

Build config: `target_os="mac" target_cpu="arm64" is_debug=false is_component_build=false`

## Run Instructions

### Quick burst loss test

```bash
./run_burst_test.sh
# Starts signaling server → receiver → sender with burst loss → kills after 30s
# Output CSV: output/session_*.csv
# Plot: python3 plot_burst_test.py output/session_latest.csv
```

### Loop over multiple videos

```bash
./run_sender_loop.sh \
  --server=127.0.0.1 \
  --port=8080 \
  --video_dir=/path/to/videos/ \
  --receiver_cmd='./out/webrtc_receiver --server=127.0.0.1 --port=8080' \
  --burst_loss='1:0.5:0.20'
```

### Manual run

```bash
# Terminal 1: signaling server
./out/peerconnection_server --port=8080

# Terminal 2: receiver (headless, writes Y4M + CSV)
./out/webrtc_receiver --server=127.0.0.1 --port=8080 --low_latency --name="rx_test"

# Terminal 3: sender (reads Y4M file, sends video)
./out/webrtc_sender --server=127.0.0.1 --port=8080 \
  --video_file=/path/to/video.y4m \
  --burst_loss="15:0.5:0.2"
```

## Key Flags

| Flag | Target | Description |
|------|--------|-------------|
| `--video_file` | sender | Path to Y4M video file |
| `--burst_loss='start:duration:pct'` | sender | Burst packet loss injection (e.g., `"15:0.5:0.2"` = at 15s, lose 20% for 0.5s) |
| `--simulate_loss` | sender | Continuous random UDP loss rate (0.0–1.0) |
| `--periodic_loss='ms:count'` | sender | Drop N packets every M ms |
| `--max_bitrate` | sender | Max video bitrate in kbps (default 1000) |
| `--low_latency` | both | Strips NACK/RTX from SDP (receiver), reduces NACK delay (sender) |
| `--play` | receiver | Display video in SDL window instead of writing to file |
| `--reconnect` | receiver | Auto-reconnect to signaling server on disconnect |
| `--name` | both | Peer name for signaling (receiver names default to `rx_<timestamp>`) |

## Architecture

### Three executables

- **`peerconnection_server`** — HTTP-based signaling server (from WebRTC examples). Peers connect via HTTP long-poll, exchange SDP/ICE candidates.
- **`webrtc_sender`** — Headless sender. Reads Y4M video file via `Y4mFrameGenerator`, creates PeerConnection, sends video. Uses `HeadlessSocketServer` (custom, avoids GUI).
- **`webrtc_receiver`** — Headless receiver. Creates PeerConnection, decodes video, writes to Y4M file (or displays via SDL). Also uses `HeadlessSocketServer`.

### Shared library: `webrtc_headless_common`

Located in `src/headless_common/`:
- `headless_socket_server.h/cc` — Custom `PhysicalSocketServer` subclass for headless operation
- `signaling_helper.h/cc` — SDP/ICE candidate JSON serialization and parsing
- `receiver_sink.h/cc` — `Y4mVideoSink` writes decoded frames to Y4M file, includes `FrameLossTracker`
- `sdl_renderer.h/mm` — SDL-based video renderer with embedded `FrameLossTracker` for sequence-number-based loss detection

### Client code: `src/client/`

- `peer_connection_client.h/cc` — Signaling client (HTTP long-poll to `peerconnection_server`)
- `defaults.h/cc` — Helper functions (`GetPeerName`, `GetPeerConnectionString`, etc.)
- `flag_defs.h` — Shared absl flag definitions (`--server`, `--port`, `--video_file`, etc.)

### Core WebRTC modifications (in webrtc-checkout)

The key instrumentation lives in the WebRTC core, not in this demo directory:

1. **RTP Header Extensions** — `FramePacketInfoExtension` (4 bytes) and `EncoderTargetBitrateExtension` (2 bytes) added to every RTP packet. Carry `total_packets`, `packet_index`, `frame_sequence` (10-bit, wraps at 1024).

2. **Sender side** — `RtpSenderVideo::SendVideo()` attaches frame packet info to each RTP packet. `frame_sequence_counter_` increments after each frame.

3. **Receiver side** — `RtpVideoStreamReceiver2::OnRtpPacket()` extracts the extension and feeds a global `PerFrameLossTracker` (atomic pointer set in receiver `main.cc`).

4. **Burst loss injection** — In `PhysicalSocket::SendTo()`, drops ~20% of RTP packets during a configurable burst window. Only drops RTP (payload 0-63, 96-127), forwards RTCP.

5. **CSV output** — `output/rtp_session_YYYYMMDD_HHMMSS.csv` with columns: `frame_seq, frame_number, total_packets, received_packets, lost_packets, loss_rate, timestamp_us`

### Data flow

```
Sender                                    Receiver
──────                                    ────────
RtpSenderVideo::SendVideo()
  -> FramePacketInfoExtension (4 bytes)
     total_packets (10 bits)
     packet_index (6 bits)
     frame_sequence (10 bits)
  -> EncoderTargetBitrateExtension (2 bytes)
     ──────────────────────────────────────▶
                                            PhysicalSocket::SendTo()
                                              -> Burst loss injection
                                            ──────────────────────────────▶
                                            RtpVideoStreamReceiver2::OnRtpPacket()
                                              -> Parse FramePacketInfoExtension
                                              -> PerFrameLossTracker groups by frame_seq
                                              -> Emits CSV on new frame arrival
```

## Source Sync Model

Source files in `src/` are copied into `../webrtc-checkout/src/examples/peerconnection/` by `build.sh` before each build:
- `src/headless_common/*` → `examples/peerconnection/headless_common/`
- `src/webrtc_sender/*` → `examples/peerconnection/webrtc_sender/`
- `src/webrtc_receiver/*` → `examples/peerconnection/webrtc_receiver/`
- `src/client/*` → `examples/peerconnection/client/`

**When editing source files, edit them in `src/` — not in the webrtc-checkout copy.** The build script handles the sync.

## Output Files

- `output/rtp_session_*.csv` — Per-frame loss data from `PerFrameLossTracker` (RTP layer)
- `output/session_*.csv` — Per-frame loss data from `FrameLossTracker` (sequence number gap analysis in renderer)
- `output/*.y4m` — Received video (when `--play` is not set)
- `webrtc_burst_test.png` — Generated plot from `plot_burst_test.py`

## Python Tools

- `plot_burst_test.py` — Generates bitrate and per-frame packet loss charts from CSV
- `udp_loss_proxy.py` — Standalone UDP proxy with configurable packet loss rate (alternative to built-in burst loss)

## Known Issues

- `total_received > total_expected` in CSV summary: RTX retransmissions inflate `received_count` beyond original `total_packets`
- `frame_sequence` wraps at 1024 (10 bits) — tracker handles this via `std::map` ordering
- CSV path is relative to receiver's working directory
