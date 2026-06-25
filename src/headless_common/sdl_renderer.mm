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

#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#endif

#include "common_video/libyuv/include/webrtc_libyuv.h"
#include "rtc_base/logging.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

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
  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp activateIgnoringOtherApps:YES];
    [[NSProcessInfo processInfo] setProcessName:@"WebRTC Receiver"];
  }
  fprintf(stderr, "[SdlRenderer] NSApplication initialized.\n");
#endif

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

  rgb_buffer_.resize(width * height * 4);
  memset(rgb_buffer_.data(), 128, rgb_buffer_.size());
  SDL_UpdateTexture(texture_, nullptr, rgb_buffer_.data(), width * 4);
  SDL_RenderClear(sdl_renderer_);
  SDL_RenderCopy(sdl_renderer_, texture_, nullptr, nullptr);
  SDL_RenderPresent(sdl_renderer_);

  SDL_PumpEvents();
  SDL_RaiseWindow(window_);
  SDL_SetWindowInputFocus(window_);
  SDL_Delay(100);
  SDL_PumpEvents();

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
  int w = frame.width();
  int h = frame.height();

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

  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_QUIT) {
      SignalQuit();
      return;
    }
  }

  static int render_count = 0;

  FrameData frame;
  int new_frames = 0;
  while (frame_buffer_.Pop(frame, 0)) {
    ++render_count;
    ++new_frames;

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
    SDL_UpdateTexture(texture_, nullptr, rgb_buffer_.data(), width_ * 4);
  }

  if (texture_) {
    SDL_RenderClear(sdl_renderer_);
    SDL_RenderCopy(sdl_renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(sdl_renderer_);
  }

  if (new_frames > 0 && render_count % 30 == 0) {
    fprintf(stderr, "[SdlRenderer] Rendered %d frames\n", render_count);
  }
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

#pragma clang diagnostic pop
