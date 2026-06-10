/*
 *  Copyright 2024 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef EXAMPLES_PEERCONNECTION_HEADLESS_COMMON_RECEIVER_SINK_H_
#define EXAMPLES_PEERCONNECTION_HEADLESS_COMMON_RECEIVER_SINK_H_

#include <cstdio>
#include <cstdint>
#include <string>

#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"

class Y4mVideoSink : public webrtc::VideoSinkInterface<webrtc::VideoFrame> {
 public:
  // Write to a regular file.
  explicit Y4mVideoSink(const std::string& filepath);
  // Write to ffplay via pipe. ffplay is started lazily on first frame
  // to avoid "Invalid argument" from premature format detection.
  explicit Y4mVideoSink(bool play_mode, const std::string& output_file);
  ~Y4mVideoSink();

  void OnFrame(const webrtc::VideoFrame& frame) override;

 private:
  void WriteHeader(int width, int height, int fps_num, int fps_den);
  void StartFfplay();

  FILE* file_ = nullptr;
  bool is_popen_ = false;
  bool header_written_ = false;
  bool play_mode_ = false;
  std::string output_file_;
  int64_t last_timestamp_us_ = 0;
};

#endif  // EXAMPLES_PEERCONNECTION_HEADLESS_COMMON_RECEIVER_SINK_H_
