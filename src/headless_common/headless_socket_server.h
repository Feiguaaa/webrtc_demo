/*
 *  Copyright 2024 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef EXAMPLES_PEERCONNECTION_HEADLESS_COMMON_HEADLESS_SOCKET_SERVER_H_
#define EXAMPLES_PEERCONNECTION_HEADLESS_COMMON_HEADLESS_SOCKET_SERVER_H_

#include <atomic>

#include "api/units/time_delta.h"
#include "rtc_base/physical_socket_server.h"
#include "rtc_base/thread.h"

extern std::atomic<bool> g_interrupted;

void SetupSignalHandler();

class HeadlessSocketServer : public webrtc::PhysicalSocketServer {
 public:
  void SetMessageQueue(webrtc::Thread* queue) override {
    message_queue_ = queue;
  }

  bool Wait(webrtc::TimeDelta max_wait_duration, bool process_io) override;

 private:
  webrtc::Thread* message_queue_ = nullptr;
};

#endif  // EXAMPLES_PEERCONNECTION_HEADLESS_COMMON_HEADLESS_SOCKET_SERVER_H_
