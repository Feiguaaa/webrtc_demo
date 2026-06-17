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
#include <cstdlib>
#include <numeric>
#include <string>

#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
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

Y4mVideoSink::Y4mVideoSink(bool play_mode, const std::string& output_file)
    : play_mode_(play_mode), output_file_(output_file) {
  if (!play_mode_) {
    file_ = fopen(output_file_.c_str(), "wb");
    if (!file_) {
      RTC_LOG(LS_ERROR) << "Failed to open output file: " << output_file_;
    }
  }
}

Y4mVideoSink::~Y4mVideoSink() {
  if (file_ && file_ != stdout) {
    if (is_popen_) {
      pclose(file_);
    } else {
      fclose(file_);
    }
  }
}

void Y4mVideoSink::StartFfplay() {
  RTC_LOG(LS_INFO) << "Starting ffplay...";
  FILE* pipe = popen("ffplay -framedrop -sync ext -f yuv4mpegpipe -i pipe:0", "w");
  if (!pipe) {
    RTC_LOG(LS_ERROR) << "Failed to start ffplay. Is it installed?";
    // Fall back to file output.
    file_ = fopen(output_file_.c_str(), "wb");
    is_popen_ = false;
    return;
  }
  file_ = pipe;
  is_popen_ = true;
  RTC_LOG(LS_INFO) << "ffplay pipe opened, file_=" << (void*)file_;
  RTC_LOG(LS_INFO) << "ffplay started for real-time playback.";
}

void Y4mVideoSink::WriteHeader(int width, int height, int fps_num, int fps_den) {
  char header[256];
  int n = snprintf(header, sizeof(header),
                   "YUV4MPEG2 W%d H%d F%d:%d Ip A0:0 C420\n", width, height,
                   fps_num, fps_den);
  if (n > 0) {
    fwrite(header, 1, n, file_);
  }
  header_written_ = true;
}

void Y4mVideoSink::OnFrame(const webrtc::VideoFrame& frame) {
  // Lazy-start ffplay on first frame to avoid "Invalid argument" error.
  // Must check this BEFORE the file_ null check, since file_ is nullptr
  // in play_mode until the first frame arrives.
  if (play_mode_ && !is_popen_) {
    StartFfplay();
    if (!file_)
      return;
  }

  if (!file_)
    return;

  auto buffer = frame.video_frame_buffer()->GetI420();
  if (!buffer) {
    RTC_LOG(LS_WARNING) << "Received non-I420 frame, skipping.";
    return;
  }

  int width = buffer->width();
  int height = buffer->height();
  int y_stride = buffer->StrideY();
  int u_stride = buffer->StrideU();
  int v_stride = buffer->StrideV();
  int uv_width = (width + 1) / 2;
  int uv_height = (height + 1) / 2;

  // Track per-frame packet loss from sequence numbers.
  loss_tracker_.OnFrameReceived(frame.packet_infos(), frame.rtp_timestamp(),
                                width, height, frame.timestamp_us());

  if (!header_written_) {
    // Estimate fps from frame timestamp. WebRTC uses microseconds.
    int fps_num = 30;
    int fps_den = 1;
    int64_t ts = frame.timestamp_us();
    if (ts > 0 && last_timestamp_us_ > 0) {
      int64_t diff = ts - last_timestamp_us_;
      if (diff > 0 && diff < 1000000) {
        fps_num = 1000000;
        fps_den = static_cast<int>(diff);
        int g = std::gcd(fps_num, fps_den);
        fps_num /= g;
        fps_den /= g;
      }
    }
    last_timestamp_us_ = ts;
    WriteHeader(width, height, fps_num, fps_den);
    fprintf(stderr,
            "[Y4mSink] DEBUG: First frame %dx%d, fps~%d/%d (%.2f), "
            "y_stride=%d u_stride=%d v_stride=%d, uv=%dx%d, play_mode=%d\n",
            width, height, fps_num, fps_den, (double)fps_num / fps_den,
            y_stride, u_stride, v_stride, uv_width, uv_height, play_mode_);
  }

  // Frame counter for tracking.
  static int frame_count = 0;
  ++frame_count;
  if (frame_count % 30 == 0) {
    fprintf(stderr, "[Y4mSink] Frame #%d, ts=%lld us\n", frame_count,
            (long long)frame.timestamp_us());
  }

  size_t y_size = 0;
  fwrite("FRAME\n", 1, 6, file_);

  // Write Y plane.
  const uint8_t* y_plane = buffer->DataY();
  if (y_stride == width) {
    y_size = fwrite(y_plane, 1, width * height, file_);
  } else {
    for (int r = 0; r < height; ++r) {
      y_size += fwrite(y_plane + r * y_stride, 1, width, file_);
    }
  }

  // Write U plane (half resolution).
  size_t u_size = 0;
  const uint8_t* u_plane = buffer->DataU();
  if (u_stride == uv_width) {
    u_size = fwrite(u_plane, 1, uv_width * uv_height, file_);
  } else {
    for (int r = 0; r < uv_height; ++r) {
      u_size += fwrite(u_plane + r * u_stride, 1, uv_width, file_);
    }
  }

  // Write V plane.
  size_t v_size = 0;
  const uint8_t* v_plane = buffer->DataV();
  if (v_stride == uv_width) {
    v_size = fwrite(v_plane, 1, uv_width * uv_height, file_);
  } else {
    for (int r = 0; r < uv_height; ++r) {
      v_size += fwrite(v_plane + r * v_stride, 1, uv_width, file_);
    }
  }

  size_t total = y_size + u_size + v_size;
  size_t expected = (size_t)width * height + 2 * (size_t)uv_width * uv_height;
  if (total != expected) {
    fprintf(stderr,
            "[Y4mSink] WARNING: Frame #%d size mismatch! "
            "expected=%zu, got=%zu (Y=%zu U=%zu V=%zu)\n",
            frame_count, expected, total, y_size, u_size, v_size);
  }

  int flush_rc = fflush(file_);
  if (flush_rc != 0 && play_mode_) {
    fprintf(stderr, "[Y4mSink] ERROR: fflush failed on frame #%d, rc=%d\n",
            frame_count, flush_rc);
    // ffplay may have exited; try to reopen.
    if (is_popen_) {
      fprintf(stderr, "[Y4mSink] ffplay pipe broken at frame #%d, stopping.\n",
              frame_count);
      pclose(file_);
      file_ = nullptr;
      is_popen_ = false;
    }
  }
}
#pragma clang diagnostic pop
