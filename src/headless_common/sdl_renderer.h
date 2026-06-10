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

struct FrameData {
  std::vector<uint8_t> bgra_data;
  int width = 0;
  int height = 0;
};

// Tracks per-frame packet loss using RTP timestamps and real packet counts
// from WebRTC's packet_infos() (populated by RtpVideoStreamReceiver2).
class FrameLossTracker {
 public:
  FrameLossTracker() = default;

  // Call for every received frame. Prints per-frame packet info.
  // frame_bytes: size of the I420 frame data (Y+U+V planes).
  // packet_count: real RTP packet count from frame.packet_infos().size().
  // time_us: frame timestamp in microseconds (for FPS calculation).
  void OnFrameReceived(uint32_t rtp_timestamp, int width, int height,
                       int frame_bytes, int packet_count, int64_t time_us);

 private:
  int frame_count_ = 0;
  uint32_t prev_rtp_timestamp_ = 0;
  int prev_width_ = 0;
  int prev_height_ = 0;

  // Interval detection (first 10 frames).
  int detected_interval_ = 0;
  int interval_samples_ = 0;
  int64_t interval_sum_ = 0;

  // Rolling average packets per frame (for estimating lost frame packets).
  double avg_packets_per_frame_ = 0.0;
  int avg_samples_ = 0;
  int64_t avg_packet_sum_ = 0;

  // Per-second frame counter.
  int64_t second_start_us_ = 0;
  int frames_this_second_ = 0;
  int prev_fps_ = 0;
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
