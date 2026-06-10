/*
 *  Copyright 2024 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "examples/peerconnection/headless_common/receiver_sink.h"

#include <cstdio>
#include <string>

#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"
#include "rtc_base/logging.h"

Y4mVideoSink::Y4mVideoSink(const std::string& filepath) {
  if (filepath == "-") {
    file_ = stdout;
  } else {
    file_ = fopen(filepath.c_str(), "wb");
    if (!file_) {
      RTC_LOG(LS_ERROR) << "Failed to open output file: " << filepath;
    }
  }
}

Y4mVideoSink::Y4mVideoSink(FILE* pipe) : file_(pipe), is_popen_(true) {}

Y4mVideoSink::~Y4mVideoSink() {
  if (file_ && file_ != stdout) {
    if (is_popen_) {
      pclose(file_);
    } else {
      fclose(file_);
    }
  }
}

void Y4mVideoSink::WriteHeader(int width, int height) {
  char header[256];
  int n = snprintf(header, sizeof(header),
                   "YUV4MPEG2 W%d H%d F30:1 Ip A0:0 C420\n", width, height);
  if (n > 0) {
    fwrite(header, 1, n, file_);
  }
  header_written_ = true;
}

void Y4mVideoSink::OnFrame(const webrtc::VideoFrame& frame) {
  if (!file_)
    return;

  auto buffer = frame.video_frame_buffer()->GetI420();
  if (!buffer) {
    RTC_LOG(LS_WARNING) << "Received non-I420 frame, skipping.";
    return;
  }

  int width = buffer->width();
  int height = buffer->height();

  if (width != last_width_ || height != last_height_) {
    RTC_LOG(LS_INFO) << "Frame resolution: " << width << "x" << height;
    last_width_ = width;
    last_height_ = height;
  }

  if (!header_written_) {
    WriteHeader(width, height);
  }

  fwrite("FRAME\n", 1, 6, file_);

  // Write Y plane.
  const uint8_t* y_plane = buffer->DataY();
  int y_stride = buffer->StrideY();
  if (y_stride == width) {
    fwrite(y_plane, 1, width * height, file_);
  } else {
    for (int r = 0; r < height; ++r) {
      fwrite(y_plane + r * y_stride, 1, width, file_);
    }
  }

  // Write U plane (half resolution).
  int uv_width = (width + 1) / 2;
  int uv_height = (height + 1) / 2;
  const uint8_t* u_plane = buffer->DataU();
  int u_stride = buffer->StrideU();
  if (u_stride == uv_width) {
    fwrite(u_plane, 1, uv_width * uv_height, file_);
  } else {
    for (int r = 0; r < uv_height; ++r) {
      fwrite(u_plane + r * u_stride, 1, uv_width, file_);
    }
  }

  // Write V plane.
  const uint8_t* v_plane = buffer->DataV();
  int v_stride = buffer->StrideV();
  if (v_stride == uv_width) {
    fwrite(v_plane, 1, uv_width * uv_height, file_);
  } else {
    for (int r = 0; r < uv_height; ++r) {
      fwrite(v_plane + r * v_stride, 1, uv_width, file_);
    }
  }
  fflush(file_);
}
