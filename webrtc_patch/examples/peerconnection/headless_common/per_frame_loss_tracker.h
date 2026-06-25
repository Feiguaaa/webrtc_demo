/*
 *  Per-frame loss tracker at the RTP packet level.
 *  Tracks ALL frames (complete and incomplete) by observing RTP packets.
 */

#ifndef EXAMPLES_PEERCONNECTION_HEADLESS_COMMON_PER_FRAME_LOSS_TRACKER_H_
#define EXAMPLES_PEERCONNECTION_HEADLESS_COMMON_PER_FRAME_LOSS_TRACKER_H_

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>

// Forward declaration.
class PerFrameLossTracker;

// Global pointer for RTP-layer per-frame loss tracking.
// Set by receiver main.cc before media starts.
// Read by RtpVideoStreamReceiver2::OnRtpPacket().
// Defined in rtp_video_stream_receiver2.cc (part of video library).
extern std::atomic<PerFrameLossTracker*> g_per_frame_loss_tracker;

// Observer called for each completed frame (complete or incomplete).
class PerFrameLossObserver {
 public:
  virtual ~PerFrameLossObserver() = default;
  virtual void OnFrameComplete(uint16_t frame_seq, uint16_t total_sent,
                               uint16_t received, int64_t timestamp_us,
                               uint16_t target_bitrate_kbps) = 0;
};

// Tracks per-frame packet loss at the RTP layer.
class PerFrameLossTracker {
 public:
  explicit PerFrameLossTracker(PerFrameLossObserver* observer);
  ~PerFrameLossTracker();

  // Call for each received RTP packet.
  void OnRtpPacket(uint16_t frame_seq, uint16_t packet_index,
                   uint16_t total_packets, int64_t timestamp_us,
                   uint16_t target_bitrate_kbps);

  void Flush();

 private:
  struct FrameState {
    uint16_t frame_seq = 0;
    uint16_t total_packets = 0;
    uint16_t max_packet_index = 0;
    uint16_t received_count = 0;
    int64_t first_timestamp_us = 0;
    bool seen_total = false;
    uint32_t seen_indices = 0;  // bitmask for dedup (covers first 32 packets)
    uint16_t target_bitrate_kbps = 0;
  };

  void EmitFrame(const FrameState& state);

  PerFrameLossObserver* observer_;
  std::map<uint16_t, FrameState> frames_;
  FILE* csv_ = nullptr;
  int frame_count_ = 0;
  int total_expected_ = 0;
  int total_received_ = 0;
  int total_lost_ = 0;
};

#endif  // EXAMPLES_PEERCONNECTION_HEADLESS_COMMON_PER_FRAME_LOSS_TRACKER_H_
