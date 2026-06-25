/*
 *  Per-frame loss tracker at the RTP packet level.
 */

#include "examples/peerconnection/headless_common/per_frame_loss_tracker.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <map>
#include <string>
#include <sys/stat.h>

PerFrameLossTracker::PerFrameLossTracker(PerFrameLossObserver* observer)
    : observer_(observer) {
  std::time_t t = std::time(nullptr);
  std::tm* tm = std::localtime(&t);
  char buf[64];
  std::snprintf(buf, sizeof(buf), "output/rtp_session_%04d%02d%02d_%02d%02d%02d.csv",
                tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                tm->tm_hour, tm->tm_min, tm->tm_sec);
  mkdir("output", 0755);
  csv_ = fopen(buf, "w");
  if (csv_) {
    fprintf(csv_, "frame_seq,frame_number,total_packets,received_packets,"
            "lost_packets,loss_rate,timestamp_us,target_bitrate_kbps\n");
    fflush(csv_);
  }
}

PerFrameLossTracker::~PerFrameLossTracker() {
  Flush();
  if (csv_) {
    fclose(csv_);
    fprintf(stderr, "[RtpLossTracker FINAL] total_expected=%d total_received=%d "
            "total_lost=%d loss_rate=%.1f%%\n",
            total_expected_, total_received_, total_lost_,
            total_expected_ > 0 ? static_cast<double>(total_lost_) / total_expected_ * 100 : 0.0);
  }
}

void PerFrameLossTracker::OnRtpPacket(uint16_t frame_seq, uint16_t packet_index,
                                       uint16_t total_packets, int64_t timestamp_us,
                                       uint16_t target_bitrate_kbps) {
  auto it = frames_.find(frame_seq);
  if (it == frames_.end()) {
    // New frame: emit all older frames first.
    for (auto& [seq, state] : frames_) {
      if (seq < frame_seq) {
        EmitFrame(state);
      }
    }
    // Remove emitted frames.
    auto next = frames_.begin();
    while (next != frames_.end() && next->first < frame_seq) {
      next = frames_.erase(next);
    }

    // Create new frame state.
    FrameState new_state;
    new_state.frame_seq = frame_seq;
    new_state.total_packets = total_packets;
    new_state.max_packet_index = packet_index;
    new_state.received_count = 1;
    new_state.first_timestamp_us = timestamp_us;
    new_state.seen_total = (total_packets > 0);
    new_state.target_bitrate_kbps = target_bitrate_kbps;
    if (packet_index < 32) {
      new_state.seen_indices = (1u << packet_index);
    }
    frames_[frame_seq] = new_state;
  } else {
    // Existing frame: deduplicate by packet_index.
    auto& state = it->second;
    if (packet_index < 32) {
      uint32_t bit = 1u << packet_index;
      if (state.seen_indices & bit) return;  // duplicate
      state.seen_indices |= bit;
    }
    state.received_count++;
    state.max_packet_index = std::max(state.max_packet_index, packet_index);
    if (total_packets > 0) {
      state.total_packets = total_packets;
      state.seen_total = true;
    }
  }
}

void PerFrameLossTracker::Flush() {
  for (auto& [seq, state] : frames_) {
    EmitFrame(state);
  }
  frames_.clear();
}

void PerFrameLossTracker::EmitFrame(const FrameState& state) {
  if (!state.seen_total) return;

  frame_count_++;
  uint16_t received = state.received_count;
  uint16_t total = state.total_packets;
  uint16_t lost = (total > received) ? (total - received) : 0;
  double loss_rate = (total > 0) ? static_cast<double>(lost) / total : 0.0;

  if (observer_) {
    observer_->OnFrameComplete(state.frame_seq, total, received,
                               state.first_timestamp_us,
                               state.target_bitrate_kbps);
  }

  if (csv_) {
    fprintf(csv_, "%u,%d,%u,%u,%u,%.4f,%lld,%u\n",
            state.frame_seq, frame_count_, total, received, lost,
            loss_rate, (long long)state.first_timestamp_us,
            state.target_bitrate_kbps);
    fflush(csv_);
  }

  total_expected_ += total;
  total_received_ += received;
  total_lost_ += lost;
}
