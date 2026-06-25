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
#include <algorithm>
#include <ctime>
#include <sys/stat.h>

// Initialize NSApplication for macOS GUI.
#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#endif

#include "common_video/libyuv/include/webrtc_libyuv.h"
#include "rtc_base/logging.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

FrameLossTracker::FrameLossTracker() {
  std::time_t t = std::time(nullptr);
  std::tm* tm = std::localtime(&t);
  char buf[64];
  std::snprintf(buf, sizeof(buf), "output/session_%04d%02d%02d_%02d%02d%02d.csv",
                tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                tm->tm_hour, tm->tm_min, tm->tm_sec);
  mkdir("output", 0755);
  OpenCsv(buf);
}

FrameLossTracker::FrameLossTracker(const std::string& output_csv) {
  OpenCsv(output_csv);
}

void FrameLossTracker::OpenCsv(const std::string& output_csv) {
  csv_ = fopen(output_csv.c_str(), "w");
  if (csv_) {
    fprintf(csv_, "frame_number,width,height,received_packets,"
            "lost_packets,loss_rate,cumulative_loss_rate,bitrate_kbps\n");
    fflush(csv_);
  }
}

FrameLossTracker::~FrameLossTracker() {
  if (csv_) {
    fclose(csv_);
  }
}

void FrameLossTracker::OnFrameReceived(const webrtc::RtpPacketInfos& packet_infos,
                                        uint32_t rtp_timestamp, int width,
                                        int height, int64_t time_us) {
  frame_count_++;

  // Per-second frame counter.
  if (second_start_us_ == 0) {
    second_start_us_ = time_us;
  }
  frames_this_second_++;
  if (time_us - second_start_us_ >= 1000000) {
    prev_fps_ = frames_this_second_;
    frames_this_second_ = 0;
    second_start_us_ = time_us;
  }

  int received_pkts = static_cast<int>(packet_infos.size());
  int64_t lost_this_frame = 0;

  if (frame_count_ == 1) {
    prev_rtp_timestamp_ = rtp_timestamp;
    prev_width_ = width;
    prev_height_ = height;
    // Seed EMA for per-frame packet count.
    avg_packets_per_frame_ = static_cast<double>(received_pkts);
    total_received_ = received_pkts;
    total_expected_ = received_pkts;
    // Seed bitrate window.
    bitrate_window_bytes_ = 0;
    // Seed EMA bitrate baseline.
    for (const auto& pkt_info : packet_infos) {
      if (pkt_info.encoder_target_bitrate_kbps()) {
        prev_bitrate_for_ema_ = *pkt_info.encoder_target_bitrate_kbps();
        ema_baseline_set_ = true;
        break;
      }
    }
  } else {
    // Try to read exact encoder bitrate from RTP header extension.
    // This is needed BEFORE loss detection to handle bitrate changes.
    int64_t exact_bitrate_kbps = 0;
    for (const auto& pkt_info : packet_infos) {
      if (pkt_info.encoder_target_bitrate_kbps()) {
        exact_bitrate_kbps = *pkt_info.encoder_target_bitrate_kbps();
        break;
      }
    }

    // Update bitrate tracking.
    int64_t current_bitrate = exact_bitrate_kbps > 0 ? exact_bitrate_kbps : prev_bitrate_kbps_;
    if (!ema_baseline_set_ && current_bitrate > 0) {
      ema_baseline_set_ = true;
      prev_bitrate_for_ema_ = current_bitrate;
    }

    // Detect significant bitrate changes and reset EMA baseline.
    // When the sender's GoogCC reduces bitrate (e.g., after burst loss),
    // it sends fewer packets per frame — this is NOT packet loss, just
    // rate adaptation. Resetting the EMA prevents false loss detection.
    bool baseline_reset = false;
    if (ema_baseline_set_ && current_bitrate > 0 && prev_bitrate_for_ema_ > 0) {
      double bitrate_ratio = static_cast<double>(current_bitrate) / prev_bitrate_for_ema_;
      if (bitrate_ratio < 0.7 || bitrate_ratio > 1.3) {
        // Bitrate changed by >30%: reset EMA to current packet count.
        avg_packets_per_frame_ = static_cast<double>(received_pkts);
        prev_bitrate_for_ema_ = current_bitrate;
        baseline_reset = true;
      }
    }

    // Detect loss using EMA-based packet count tracking.
    // Skipped on baseline reset frames — those are rate adaptation, not loss.
    // We can't use sequence number gaps because the sender's pacer sends
    // padding/probe packets that consume sequence numbers but don't appear
    // in frame.packet_infos(). Instead, we detect sudden drops in received
    // packet count compared to the rolling average.
    if (!baseline_reset) {
      double deficit = avg_packets_per_frame_ - static_cast<double>(received_pkts);
      if (deficit > avg_packets_per_frame_ * 0.35) {
        // More than 35% below average → count deficit as lost packets.
        lost_this_frame = static_cast<int64_t>(deficit + 0.5);
      }

      total_expected_ += received_pkts + lost_this_frame;
      total_received_ += received_pkts;
      cumulative_lost_ += lost_this_frame;
    } else {
      total_expected_ += received_pkts;
      total_received_ += received_pkts;
    }

    // Update EMA: exponential moving average of packets per frame.
    constexpr double kAlpha = 0.15;
    avg_packets_per_frame_ = kAlpha * static_cast<double>(received_pkts) +
                             (1.0 - kAlpha) * avg_packets_per_frame_;

    // RTP timestamp delta (kept for bitrate time calculation).
    uint32_t delta = rtp_timestamp - prev_rtp_timestamp_;
    int64_t frame_time_us = static_cast<int64_t>(delta) * 1000000LL / 90000LL;

    if (exact_bitrate_kbps > 0) {
      prev_bitrate_kbps_ = exact_bitrate_kbps;
    } else {
      // Fallback: estimate from RTP packet count × MTU.
      static constexpr int kRtpPacketPayloadBytes = 1200;
      bitrate_window_bytes_ += received_pkts * kRtpPacketPayloadBytes;
      bitrate_window_time_us_ += frame_time_us;
      bitrate_window_frames_++;
      bitrate_window_packets_ += received_pkts;

      if (bitrate_window_frames_ >= 30) {
        if (bitrate_window_time_us_ > 0) {
          prev_bitrate_kbps_ = bitrate_window_bytes_ * 8000LL / bitrate_window_time_us_;
        }
        bitrate_window_bytes_ = 0;
        bitrate_window_time_us_ = 0;
        bitrate_window_frames_ = 0;
        bitrate_window_packets_ = 0;
      }
    }
  }

  // Per-frame loss rate (instantaneous and cumulative).
  double frame_loss_rate = 0.0;
  double cumulative_loss_rate = 0.0;
  if (total_expected_ > 0) {
    cumulative_loss_rate =
        static_cast<double>(cumulative_lost_) / total_expected_;
  }
  if (lost_this_frame > 0 && (received_pkts + lost_this_frame) > 0) {
    frame_loss_rate =
        static_cast<double>(lost_this_frame) / (received_pkts + lost_this_frame);
  }

  // Write to CSV.
  if (csv_) {
    fprintf(csv_, "%d,%d,%d,%d,%ld,%.4f,%.4f,%ld\n",
            frame_count_, width, height, received_pkts,
            (long)lost_this_frame, frame_loss_rate, cumulative_loss_rate,
            (long)prev_bitrate_kbps_);
    fflush(csv_);
  }

  // Print summary every 30 frames.
  if (frame_count_ % 30 == 0) {
    fprintf(stderr,
            "[LossTracker] Frame #%d | %dx%d | "
            "pkts=%d | fps=%d | bitrate=%ld kbps | "
            "expected=%lld recv=%lld lost=%lld cum_loss=%.1f%%\n",
            frame_count_, width, height, received_pkts,
            prev_fps_, (long)prev_bitrate_kbps_,
            (long long)total_expected_, (long long)total_received_,
            (long long)cumulative_lost_,
            cumulative_loss_rate * 100);
  }

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

  int w = frame.width();
  int h = frame.height();

  // RTP timestamp-based loss tracking (every frame).
  loss_tracker_.OnFrameReceived(frame.packet_infos(), frame.rtp_timestamp(), w, h,
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
  int new_frames = 0;
  while (frame_buffer_.Pop(frame, 0)) {
    ++render_count;
    ++new_frames;

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

  // Print only when new frames were rendered this call.
  if (new_frames > 0 && render_count % 30 == 0) {
    fprintf(stderr, "[SdlRenderer] Rendered %d frames\n", render_count);
  }
}

#pragma clang diagnostic pop
