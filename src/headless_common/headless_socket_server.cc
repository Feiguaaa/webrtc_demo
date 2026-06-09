/*
 *  Copyright 2024 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "examples/peerconnection/headless_common/headless_socket_server.h"

#include <csignal>

std::atomic<bool> g_interrupted{false};

extern "C" void SignalHandler(int /*signum*/) {
  g_interrupted = true;
}

void SetupSignalHandler() {
  struct sigaction sa;
  sa.sa_handler = SignalHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
}

bool HeadlessSocketServer::Wait(webrtc::TimeDelta max_wait_duration,
                                 bool process_io) {
  if (g_interrupted) {
    if (message_queue_) {
      message_queue_->Quit();
    }
    return false;
  }
  webrtc::TimeDelta capped = max_wait_duration;
  if (!max_wait_duration.IsFinite() ||
      max_wait_duration > webrtc::TimeDelta::Millis(100)) {
    capped = webrtc::TimeDelta::Millis(100);
  }
  return PhysicalSocketServer::Wait(capped, process_io);
}
