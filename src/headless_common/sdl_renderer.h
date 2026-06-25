/*
 *  Copyright 2026 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef EXAMPLES_PEERCONNECTION_HEADLESS_COMMON_SDL_RENDERER_H_
#define EXAMPLES_PEERCONNECTION_HEADLESS_COMMON_SDL_RENDERER_H_

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"
#include "api/rtp_packet_infos.h"

struct FrameData {
  std::vector<uint8_t> bgra_data;
  int width = 0;
  int height = 0;
};

// Tracks packet loss by comparing RTP sequence numbers across frames.
// Expected packets = seq gap between consecutive frames.
// Lost packets = expected - received.
class FrameLossTracker {
 public:
  FrameLossTracker();
  explicit FrameLossTracker(const std::string& output_csv);
  ~FrameLossTracker();

  // Call for every received frame. Detects packet loss by
  // comparing RTP sequence number gaps between consecutive frames.
  void OnFrameReceived(const webrtc::RtpPacketInfos& packet_infos,
                       uint32_t rtp_timestamp, int width, int height,
                       int64_t time_us);

 private:
  void OpenCsv(const std::string& output_csv);

  int frame_count_ = 0;
  uint32_t prev_rtp_timestamp_ = 0;
  int prev_width_ = 0;
  int prev_height_ = 0;

  // Rolling count of expected vs received packets.
  int64_t total_expected_ = 0;
  int64_t total_received_ = 0;
  int64_t cumulative_lost_ = 0;

  // EMA-based per-frame packet count tracking.
  double avg_packets_per_frame_ = 0.0;

  // Bitrate baseline for EMA (prevents false loss after bitrate changes).
  int64_t prev_bitrate_for_ema_ = 0;
  bool ema_baseline_set_ = false;

  // Per-second frame counter.
  int64_t second_start_us_ = 0;
  int frames_this_second_ = 0;
  int prev_fps_ = 0;

  // Rolling bitrate calculation (30-frame window).
  int64_t bitrate_window_bytes_ = 0;
  int64_t bitrate_window_time_us_ = 0;
  int bitrate_window_frames_ = 0;
  int bitrate_window_packets_ = 0;
  int64_t prev_bitrate_kbps_ = 0;

  // CSV output for plotting.
  FILE* csv_ = nullptr;
};

class FrameBuffer {
 public:
  void Push(FrameData frame);
  bool Pop(FrameData& out, int timeout_ms);
  void SignalQuit();

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  FrameData frame_;
  bool quit_ = false;
};

class SdlVideoRenderer : public webrtc::VideoSinkInterface<webrtc::VideoFrame> {
 public:
  // Constructor MUST be called from the main thread (creates SDL window).
  SdlVideoRenderer(const char* title, int width, int height);
  ~SdlVideoRenderer();

  void OnFrame(const webrtc::VideoFrame& frame) override;

  // Call this from the main thread periodically to process SDL events
  // and render pending frames.
  void RenderPendingFrames();

  // Signal the renderer to quit (called from signal handler).
  void SignalQuit();

  bool initialized() const { return window_ != nullptr; }

 private:
  struct SDL_Window* window_ = nullptr;
  struct SDL_Renderer* sdl_renderer_ = nullptr;
  struct SDL_Texture* texture_ = nullptr;
  int width_ = 0;
  int height_ = 0;
  std::vector<uint8_t> rgb_buffer_;
  FrameBuffer frame_buffer_;
  FrameLossTracker loss_tracker_;
  int on_frame_count_ = 0;
};

#endif  // EXAMPLES_PEERCONNECTION_HEADLESS_COMMON_SDL_RENDERER_H_
