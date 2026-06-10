/*
 *  Copyright 2026 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "examples/peerconnection/headless_common/sdl_renderer.h"

#include <SDL.h>
#include <signal.h>

// Initialize NSApplication for macOS GUI.
#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#endif

#include "common_video/libyuv/include/webrtc_libyuv.h"
#include "rtc_base/logging.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

void FrameLossTracker::OnFrameReceived(uint32_t rtp_timestamp, int width,
                                        int height, int frame_bytes,
                                        int packet_count, int64_t time_us) {
  frame_count_++;

  // Per-second frame counter (uses wall clock from frame timestamp).
  if (second_start_us_ == 0) {
    second_start_us_ = time_us;
  }
  frames_this_second_++;
  if (time_us - second_start_us_ >= 1000000) {
    prev_fps_ = frames_this_second_;
    frames_this_second_ = 0;
    second_start_us_ = time_us;
  }

  // Update rolling average packets per frame (from real packet count).
  avg_samples_++;
  avg_packet_sum_ += packet_count;
  if (avg_samples_ >= 30) {
    avg_packets_per_frame_ = static_cast<double>(avg_packet_sum_) / avg_samples_;
    avg_samples_ = 0;
    avg_packet_sum_ = 0;
  }

  if (frame_count_ == 1) {
    prev_rtp_timestamp_ = rtp_timestamp;
    prev_width_ = width;
    prev_height_ = height;
    fprintf(stderr,
            "[LossTracker] Frame #%d | %dx%d | "
            "real_pkts=%d recv / %d total / 0 lost | fps=%d\n",
            frame_count_, width, height, packet_count, packet_count, prev_fps_);
    return;
  }

  // RTP timestamp delta.
  uint32_t delta = rtp_timestamp - prev_rtp_timestamp_;

  // Detect expected interval from first 10 deltas.
  if (interval_samples_ < 10 && delta > 0 && delta < 100000) {
    interval_sum_ += delta;
    interval_samples_++;
    if (interval_samples_ == 10) {
      detected_interval_ = static_cast<int>(interval_sum_ / 10);
      fprintf(stderr,
              "[LossTracker] Detected RTP interval: %d (~%.1f fps)\n",
              detected_interval_, 90000.0 / detected_interval_);
    }
  }

  // Calculate lost frames from RTP timestamp gap.
  int lost_frames = 0;
  int lost_packets = 0;
  if (detected_interval_ > 0 && delta > 0) {
    int expected_intervals = static_cast<int>(delta / detected_interval_);
    lost_frames = expected_intervals - 1;
    if (lost_frames > 0) {
      lost_packets = static_cast<int>(lost_frames * avg_packets_per_frame_);
    }
  }

  int total_packets = packet_count + lost_packets;
  int recv_packets = packet_count;

  fprintf(stderr,
          "[LossTracker] Frame #%d | %dx%d | "
          "real_pkts=%d | pkts=%d recv / %d total / %d lost | "
          "fps=%d\n",
          frame_count_, width, height,
          packet_count,
          recv_packets, total_packets, lost_packets, prev_fps_);

  prev_rtp_timestamp_ = rtp_timestamp;
  prev_width_ = width;
  prev_height_ = height;
}

void FrameBuffer::Push(FrameData frame) {
  std::lock_guard<std::mutex> lock(mutex_);
  frame_ = std::move(frame);
  cv_.notify_one();
}

bool FrameBuffer::Pop(FrameData& out, int timeout_ms) {
  std::unique_lock<std::mutex> lock(mutex_);
  if (cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                   [this] { return frame_.width > 0 || quit_; })) {
    if (quit_)
      return false;
    out = std::move(frame_);
    frame_.width = 0;
    frame_.height = 0;
    frame_.bgra_data.clear();
    return true;
  }
  return false;
}

void FrameBuffer::SignalQuit() {
  std::lock_guard<std::mutex> lock(mutex_);
  quit_ = true;
  cv_.notify_one();
}

// Global pointer for signal handler.
static SdlVideoRenderer* g_renderer = nullptr;

static void SignalHandler(int signum) {
  if (g_renderer) {
    g_renderer->SignalQuit();
  }
}

SdlVideoRenderer::SdlVideoRenderer(const char* title, int width, int height) {
  g_renderer = this;

  fprintf(stderr, "[SdlRenderer] Creating SDL window %dx%d...\n", width, height);

#ifdef __APPLE__
  // Initialize NSApplication so macOS can display windows properly.
  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp activateIgnoringOtherApps:YES];
    // Set the app name for the menu bar.
    [[NSProcessInfo processInfo] setProcessName:@"WebRTC Receiver"];
  }
  fprintf(stderr, "[SdlRenderer] NSApplication initialized.\n");
#endif

  // SDL_Init and window creation MUST happen on the main thread on macOS.
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    fprintf(stderr, "[SdlRenderer] SDL_Init failed: %s\n", SDL_GetError());
    RTC_LOG(LS_ERROR) << "SDL_Init failed: " << SDL_GetError();
    return;
  }

  window_ = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED, width, height,
                             SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
  if (!window_) {
    fprintf(stderr, "[SdlRenderer] SDL_CreateWindow failed: %s\n", SDL_GetError());
    RTC_LOG(LS_ERROR) << "SDL_CreateWindow failed: " << SDL_GetError();
    SDL_Quit();
    return;
  }
  fprintf(stderr, "[SdlRenderer] Window created, id=%u\n", SDL_GetWindowID(window_));

  sdl_renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
  if (!sdl_renderer_) {
    fprintf(stderr, "[SdlRenderer] SDL_CreateRenderer failed: %s\n", SDL_GetError());
    RTC_LOG(LS_ERROR) << "SDL_CreateRenderer failed: " << SDL_GetError();
    SDL_DestroyWindow(window_);
    SDL_Quit();
    window_ = nullptr;
    return;
  }

  texture_ = SDL_CreateTexture(sdl_renderer_, SDL_PIXELFORMAT_ARGB8888,
                               SDL_TEXTUREACCESS_STREAMING, width, height);
  if (!texture_) {
    fprintf(stderr, "[SdlRenderer] SDL_CreateTexture failed: %s\n", SDL_GetError());
    RTC_LOG(LS_ERROR) << "SDL_CreateTexture failed: " << SDL_GetError();
    SDL_DestroyRenderer(sdl_renderer_);
    SDL_DestroyWindow(window_);
    SDL_Quit();
    sdl_renderer_ = nullptr;
    window_ = nullptr;
    return;
  }

  // Draw a blank gray screen so we can see the window is working.
  rgb_buffer_.resize(width * height * 4);
  memset(rgb_buffer_.data(), 128, rgb_buffer_.size());
  SDL_UpdateTexture(texture_, nullptr, rgb_buffer_.data(), width * 4);
  SDL_RenderClear(sdl_renderer_);
  SDL_RenderCopy(sdl_renderer_, texture_, nullptr, nullptr);
  SDL_RenderPresent(sdl_renderer_);

  // Pump events so macOS processes the window creation immediately.
  SDL_PumpEvents();

  // Force window to front and give it focus.
  SDL_RaiseWindow(window_);
  SDL_SetWindowInputFocus(window_);

  // Small delay to let macOS bring the window on screen.
  SDL_Delay(100);

  // Pump again after raise.
  SDL_PumpEvents();

  // Print window position for debugging.
  int wx = 0, wy = 0;
  SDL_GetWindowPosition(window_, &wx, &wy);
  fprintf(stderr, "[SdlRenderer] SDL fully initialized, window at (%d,%d) %dx%d\n",
          wx, wy, width, height);

  signal(SIGINT, SignalHandler);
  signal(SIGTERM, SignalHandler);

  RTC_LOG(LS_INFO) << "SDL renderer initialized: " << width << "x" << height;
}

SdlVideoRenderer::~SdlVideoRenderer() {
  frame_buffer_.SignalQuit();
  g_renderer = nullptr;

  if (texture_) {
    SDL_DestroyTexture(texture_);
    texture_ = nullptr;
  }
  if (sdl_renderer_) {
    SDL_DestroyRenderer(sdl_renderer_);
    sdl_renderer_ = nullptr;
  }
  if (window_) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }
  SDL_Quit();
}

void SdlVideoRenderer::SignalQuit() {
  frame_buffer_.SignalQuit();
}

void SdlVideoRenderer::OnFrame(const webrtc::VideoFrame& frame) {
  ++on_frame_count_;

  // Compute I420 frame size for bitrate estimation.
  int w = frame.width();
  int h = frame.height();
  int i420_size = w * h + 2 * ((w + 1) / 2) * ((h + 1) / 2);

  // Get real RTP packet count from WebRTC's internal tracking.
  size_t packet_count = frame.packet_infos().size();

  // RTP timestamp-based loss tracking (every frame).
  loss_tracker_.OnFrameReceived(frame.rtp_timestamp(), w, h,
                                i420_size, static_cast<int>(packet_count),
                                frame.timestamp_us());

  // Called on the WebRTC decode thread.
  // Convert I420 to ARGB and push to the buffer.
  FrameData data;
  data.width = w;
  data.height = h;
  data.bgra_data.resize(w * h * 4);

  int rc = webrtc::ConvertFromI420(frame, webrtc::VideoType::kARGB, 0,
                                   data.bgra_data.data());
  if (rc < 0) {
    fprintf(stderr, "[SdlRenderer] ConvertFromI420 failed: %d\n", rc);
    RTC_LOG(LS_WARNING) << "ConvertFromI420 failed: " << rc;
    return;
  }

  frame_buffer_.Push(std::move(data));
}

void SdlVideoRenderer::RenderPendingFrames() {
  if (!window_)
    return;

  static int render_count = 0;

  // Always pump SDL events (required for macOS window to stay responsive).
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_QUIT) {
      SignalQuit();
      return;
    }
  }

  // Render all pending frames (keep only the latest).
  FrameData frame;
  while (frame_buffer_.Pop(frame, 0)) {
    ++render_count;

    // Reallocate buffer and texture if dimensions changed.
    if (frame.width != width_ || frame.height != height_) {
      width_ = frame.width;
      height_ = frame.height;
      rgb_buffer_.resize(width_ * height_ * 4);

      if (texture_) {
        SDL_DestroyTexture(texture_);
      }
      texture_ = SDL_CreateTexture(sdl_renderer_, SDL_PIXELFORMAT_ARGB8888,
                                   SDL_TEXTUREACCESS_STREAMING, width_,
                                   height_);
      if (!texture_) {
        fprintf(stderr, "[SdlRenderer] SDL_CreateTexture failed on resize: %s\n",
                SDL_GetError());
        RTC_LOG(LS_ERROR) << "SDL_CreateTexture failed on resize: "
                          << SDL_GetError();
        return;
      }
      fprintf(stderr, "[SdlRenderer] Resized to %dx%d\n", width_, height_);
      RTC_LOG(LS_INFO) << "SDL resized to " << width_ << "x" << height_;
    }

    rgb_buffer_.assign(frame.bgra_data.begin(), frame.bgra_data.end());

    // Upload the new frame data to the SDL texture.
    SDL_UpdateTexture(texture_, nullptr, rgb_buffer_.data(), width_ * 4);
  }

  // Always re-present the current texture (keeps window alive on macOS).
  if (texture_) {
    SDL_RenderClear(sdl_renderer_);
    SDL_RenderCopy(sdl_renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(sdl_renderer_);
  }

  if (render_count > 0 && render_count % 30 == 0) {
    fprintf(stderr, "[SdlRenderer] Rendered %d frames\n", render_count);
  }
}

#pragma clang diagnostic pop
