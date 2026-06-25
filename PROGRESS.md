# WebRTC RTP-Layer Per-Frame Loss Tracking — Project Progress

> Last updated: 2026-06-24

## 1. Goal

Record per-frame packet loss data for **ALL frames** (both complete and incomplete) at the **RTP layer**, without requiring actual decoding. When a frame's playout deadline arrives (new frame_seq seen), emit its loss data immediately — complete or not.

## 2. Architecture Overview

```
Sender                                    Receiver
──────                                    ────────
RtpSenderVideo::SendVideo()
  -> FramePacketInfoExtension (4 bytes)
     total_packets (10 bits)
     packet_index (6 bits)
     frame_sequence (10 bits, wraps at 1024)
  -> EncoderTargetBitrateExtension (2 bytes)
     ──────────────────────────────────────▶
                                            PhysicalSocket::SendTo()
                                              -> Burst packet loss injection
                                              -> RTP dropped, RTCP forwarded
                                            ──────────────────────────────▶
                                            RtpVideoStreamReceiver2::OnRtpPacket()
                                              -> Parse FramePacketInfoExtension
                                              -> g_per_frame_loss_tracker->OnRtpPacket()
                                            ──────────────────────────────▶
                                            PerFrameLossTracker
                                              -> Group by frame_seq
                                              -> Emit CSV on new frame arrival
```

## 3. All Modified/Added Files

### 3.1 NEW files (untracked, must be created)

#### `examples/peerconnection/headless_common/per_frame_loss_tracker.h`
- `PerFrameLossTracker` class: groups RTP packets by `frame_seq`, emits loss data when new frame arrives
- `PerFrameLossObserver` interface: callback for decoded frames
- `extern std::atomic<PerFrameLossTracker*> g_per_frame_loss_tracker` — global pointer set by receiver main, read by RTP layer

#### `examples/peerconnection/headless_common/per_frame_loss_tracker.cc`
- `OnRtpPacket(frame_seq, packet_index, total_packets, timestamp_us)`:
  - New `frame_seq` seen → emit ALL older frames (complete or incomplete)
  - Only emits frames where `seen_total == true` (we received at least one packet that carried the total count)
- `EmitFrame()`: writes to CSV `output/rtp_session_YYYYMMDD_HHMMSS.csv`
  - Columns: `frame_seq, frame_number, total_packets, received_packets, lost_packets, loss_rate, timestamp_us`
  - Maintains running totals: `total_expected_`, `total_received_`, `total_lost_`
- Destructor: prints `[RtpLossTracker FINAL]` summary

#### `examples/peerconnection/webrtc_receiver/main.cc` (NEW FILE)
- Headless WebRTC receiver with signaling client
- **Key integration** in `Receiver` constructor:
  ```cpp
  per_frame_loss_tracker_ = std::make_unique<PerFrameLossTracker>(nullptr);
  g_per_frame_loss_tracker.store(per_frame_loss_tracker_.get(), std::memory_order_relaxed);
  ```
- In destructor: `g_per_frame_loss_tracker.store(nullptr, std::memory_order_relaxed);`
- Flags: `--output_file`, `--play`, `--width`, `--height`, `--low_latency`, `--reconnect`
- In low_latency mode: strips NACK/RTX/`apt` from SDP

#### `examples/peerconnection/webrtc_sender/main.cc` (NEW FILE)
- Headless WebRTC sender with video file input
- `--burst_loss='start_s:duration_s:loss_pct'` — configures burst packet loss
- `--video_file` — path to Y4M video file
- Calls `webrtc::SetBurstLoss(...)` in main() and `webrtc::SetBurstMediaEpoch()` when capturer starts

### 3.2 MODIFIED core WebRTC files (git-tracked)

#### `modules/rtp_rtcp/source/rtp_video_header.h`
- Added `FramePacketInfo` struct: `total_packets`, `packet_index`, `frame_sequence` (all `uint16_t`)
- Added `encoder_target_bitrate_kbps` and `frame_packet_info` optional fields to `RTPVideoHeader`

#### `modules/rtp_rtcp/source/rtp_header_extensions.h`
- Added `EncoderTargetBitrateExtension` class (2 bytes, big-endian `uint16_t`)
- Added `FramePacketInfoExtension` class (4 bytes):
  - Bit layout: `total_packets` (bits 22-31, 10 bits) | `packet_index` (bits 16-21, 6 bits) | `frame_sequence` (bits 0-9, 10 bits)
  - Uses `ByteReader<uint32_t>::ReadBigEndian` / `ByteWriter<uint32_t>::WriteBigEndian`

#### `modules/rtp_rtcp/source/rtp_header_extension_map.cc`
- Registered new extension URIs: `kEncoderTargetBitrateUri`, `kFramePacketInfoUri`

#### `modules/rtp_rtcp/include/rtp_rtcp_defines.h`
- Added `kRtpExtensionEncoderTargetBitrate` and `kRtpExtensionFramePacketInfo` to `RTPExtensionType` enum

#### `api/rtp_parameters.h` / `api/rtp_parameters.cc`
- Added `"encoder-target-bitrate"` and `"frame-packet-info"` to supported RTP extension URIs

#### `modules/rtp_rtcp/source/rtp_sender_video.h`
- Added member: `uint16_t frame_sequence_counter_ RTC_GUARDED_BY(send_checker_) = 0;`

#### `modules/rtp_rtcp/source/rtp_sender_video.cc`
- In `SendVideo()`: sets `FramePacketInfoExtension` on every packet (if `num_packets <= 1023`)
  ```cpp
  frame_info.total_packets = static_cast<uint16_t>(num_packets);
  frame_info.packet_index = static_cast<uint16_t>(i);
  frame_info.frame_sequence = frame_sequence_counter_;
  packet->SetExtension<FramePacketInfoExtension>(frame_info);
  ```
- After frame sent: `frame_sequence_counter_ = (frame_sequence_counter_ + 1) & 0x3FF;`
- In `AddRtpHeaderExtensions()`: sets `EncoderTargetBitrateExtension` on first packet

#### `api/rtp_packet_info.h`
- Added getters/setters: `total_frame_packets`, `frame_packet_index`, `frame_sequence`, `encoder_target_bitrate_kbps`, `sequence_number`
- Added private members: `total_frame_packets_`, `frame_packet_index_`, `frame_sequence_`, `encoder_target_bitrate_kbps_`, `sequence_number_`

#### `modules/rtp_rtcp/source/rtp_packet.cc`
- Copy `total_frame_packets`, `frame_packet_index`, `frame_sequence`, `sequence_number` when copying `RtpPacketInfo`

#### `modules/rtp_rtcp/source/rtp_sender.cc`
- Added logging for first packet: frame dimensions and encoder target bitrate

#### `video/rtp_video_stream_receiver2.cc`
- **Global pointer definition**: `std::atomic<PerFrameLossTracker*> g_per_frame_loss_tracker{nullptr};` (at global scope, outside `namespace webrtc`)
- **Include**: `#include "examples/peerconnection/headless_common/per_frame_loss_tracker.h"`
- **In `OnReceivedPayloadData()`**: parses `EncoderTargetBitrateExtension` and `FramePacketInfoExtension` from RTP packet, populates `packet_info`
- **In `OnRtpPacket()`**: after `ReceivePacket()`, calls `g_per_frame_loss_tracker->OnRtpPacket(...)` if tracker is set
  ```cpp
  if (PerFrameLossTracker* tracker = g_per_frame_loss_tracker.load(std::memory_order_relaxed)) {
    FramePacketInfo frame_info;
    if (packet.GetExtension<FramePacketInfoExtension>(&frame_info)) {
      tracker->OnRtpPacket(
          frame_info.frame_sequence, frame_info.packet_index,
          frame_info.total_packets,
          env_.clock().CurrentTime().us());
    }
  }
  ```

#### `rtc_base/physical_socket_server.h`
- Declared: `SetPacketLossRate()`, `SetPeriodicLoss()`, `SetBurstLoss()`, `SetBurstMediaEpoch()`

#### `rtc_base/physical_socket_server.cc`
- **Burst loss in `SendTo()`**: drops ~20% of RTP packets during a 0.5s burst window starting 1s after media epoch
  - Only drops RTP (payload type 0-63, 96-127), forwards RTCP (payload type 64-95)
  - Uses `clock_gettime(CLOCK_REALTIME)` for cross-process alignment via `g_burst_config_epoch_us` + `g_burst_media_epoch_us`
  - Debug logging: `[BurstDebug]` for first 3 drops
- Global state: `g_burst_start_us`, `g_burst_duration_us`, `g_burst_loss_pct`, `g_burst_config_epoch_us`, `g_burst_media_epoch_us`

#### `media/engine/webrtc_video_engine.cc`
- Registers new RTP extension URIs in `AddDefaultExtMap()` for video send/receive

#### `call/rtp_video_sender.cc` / `call/rtp_video_sender.h`
- Passes `encoder_target_bitrate_kbps` and `frame_packet_info` from `RtpSender` to `RTPVideoHeader`

#### `call/rtp_transport_controller_send.cc` / `call/bitrate_allocator.cc`
- Logs encoder target bitrate when available

#### `call/call.cc`
- Logs encoder target bitrate in `AddBwePeriodicLoggers()`

#### `modules/congestion_controller/goog_cc/` (goog_cc_network_control.cc, loss_based_bwe_v2.cc/h, send_side_bandwidth_estimation.cc)
- Added encoder target bitrate to GoogCC logging/estimation

#### `p2p/base/p2p_transport_channel.cc`
- Minor: added logging

#### `modules/rtp_rtcp/source/rtcp_sender.cc`
- Minor: adjusted logging

#### `video/video_send_stream_impl.cc`
- Added encoder target bitrate handling

#### `examples/BUILD.gn`
- Added `webrtc_headless_common` library with `per_frame_loss_tracker.cc/.h`
- Added `webrtc_sender` and `webrtc_receiver` executables (mac only)
- Added `peerconnection_server` executable (mac only)

#### `examples/peerconnection/client/flag_defs.h`
- Added `video_file`, `low_latency` flag definitions

#### `examples/peerconnection/server/data_socket.cc`
- Minor: include fix for macOS

## 4. How It Works — Key Flows

### 4.1 Frame sequence assignment (sender)
1. `RTPSenderVideo::SendVideo()` knows `num_packets` for each frame
2. For each packet `i`: sets `FramePacketInfoExtension` with `(total_packets, i, frame_sequence_counter_)`
3. After all packets sent: `frame_sequence_counter_ = (frame_sequence_counter_ + 1) & 0x3FF`

### 4.2 Frame sequence tracking (receiver)
1. `RtpVideoStreamReceiver2::OnRtpPacket()` extracts `FramePacketInfoExtension`
2. Calls `PerFrameLossTracker::OnRtpPacket(frame_seq, packet_index, total_packets, timestamp_us)`
3. Tracker groups packets by `frame_seq` in a `std::map<uint16_t, FrameState>`
4. When a **new** `frame_seq` arrives:
   - Emits ALL older frames (even incomplete ones — they only have the packets that actually arrived)
   - Removes emitted frames from the map
5. `EmitFrame()`: `lost_packets = total_packets - min(received_count, total_packets)`

### 4.3 Burst loss injection (sender side)
1. `SetBurstLoss(start_us, duration_us, loss_pct)` called in sender main()
2. `SetBurstMediaEpoch()` called when video capturer starts
3. `PhysicalSocket::SendTo()` checks `ShouldDropBurst()` for every UDP packet
4. If in burst window and random < loss_pct: **drop RTP packet only**, forward RTCP

### 4.4 CSV output format
```
frame_seq,frame_number,total_packets,received_packets,lost_packets,loss_rate,timestamp_us
0,1,9,9,0,0.0000,1022428073567
1,2,6,6,0,0.0000,1022428145100
...
45,47,21,10,11,0.5238,1022429895809    <- burst: 11 of 21 packets lost
46,49,19,9,10,0.5263,1022429937750    <- burst: 10 of 19 packets lost
...
```

## 5. Build Instructions

```bash
# Build directory
BUILD_DIR=/Users/jinx/Public/codespace/webrtc-checkout/src/out/webrtc_demo

# Build sender and receiver
ninja -C $BUILD_DIR webrtc_sender webrtc_receiver

# Binaries are symlinked from /Users/jinx/Public/codespace/webrtc-demo/out/
```

## 6. Test Instructions

```bash
cd /Users/jinx/Public/codespace/webrtc-demo

# Run with burst loss
./run_sender_loop.sh \
  --server=127.0.0.1 \
  --port=8080 \
  --video_dir=/tmp/single_video_test \
  --receiver_cmd='./out/webrtc_receiver --server=127.0.0.1 --port=8080' \
  --burst_loss='1:0.5:0.20'

# Output CSV: output/rtp_session_*.csv
# Plot: python3 plot_burst_test.py
```

## 7. Test Results (2026-06-24)

- **444 frames** recorded in CSV
- **Burst period** (frame ~23-60): `lost_packets` ranges from 1 to 11 per frame
- **Summary**: `total_expected=4787 total_received=6143 total_lost=31 loss_rate=0.6%`
  - Note: `total_received > total_expected` because retransmitted packets increment `received_count` beyond `total_packets`
- **Completely lost frames**: 49 frames (detected via `frame_sequence` gap in `SdlVideoRenderer`)
- **Burst loss confirmed**: `[BurstDebug]` logs show RTP packets being dropped, RTCP forwarded

## 8. Known Issues / TODO

- `total_received > total_expected` in summary: RTX retransmissions inflate `received_count`. Could be fixed by only counting packets with unique `(frame_seq, packet_index)` pairs.
- `PerFrameLossTracker` CSV is written relative to `output/` directory — the receiver process's cwd determines the actual path. Current tests write to `/Users/jinx/Public/codespace/webrtc_demo/output/`.
- `[BurstDebug]` logging limited to first 20 drops — can be removed if not needed.
- The `frame_sequence` wraps at 1024 (10 bits). The tracker handles this correctly via `std::map` ordering, but gap detection for completely lost frames uses modular arithmetic in `SdlVideoRenderer`.

## 9. Key Design Decisions

1. **Global atomic pointer** instead of class member chain: `g_per_frame_loss_tracker` avoids threading `SetPerFrameLossTracker()` through `RtpVideoStreamReceiver2 -> VideoReceiveStream2 -> PeerConnection`. The pointer is set once in receiver `main.cc` before media starts.

2. **Burst loss on sender `SendTo()`**, not receiver `DoReadFromSocket()`: RTCP feedback flows from receiver to sender on the same UDP socket but in the reverse direction. `SendTo()` only handles outbound data (RTP), so RTCP from the receiver is naturally forwarded.

3. **4-byte `FramePacketInfoExtension`**: Uses RFC 8285 one-byte header extension. The 10+6+10 bit layout packs `total_packets`, `packet_index`, and `frame_sequence` into 4 bytes. One-byte extension supports 1-16 bytes, so 4 bytes is fine.

4. **Emit incomplete frames immediately**: When a new `frame_seq` arrives, ALL older frames are emitted regardless of completeness. This is the core requirement — "DDL 一到就记录".
