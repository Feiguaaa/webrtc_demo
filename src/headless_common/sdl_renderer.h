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
  SdlVideoRenderer(const char* title, int width, int height);
  ~SdlVideoRenderer();

  void OnFrame(const webrtc::VideoFrame& frame) override;

  void RenderPendingFrames();

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
};

#endif  // EXAMPLES_PEERCONNECTION_HEADLESS_COMMON_SDL_RENDERER_H_
