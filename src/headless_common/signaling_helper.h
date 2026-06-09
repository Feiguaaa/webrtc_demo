/*
 *  Copyright 2024 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef EXAMPLES_PEERCONNECTION_HEADLESS_COMMON_SIGNALING_HELPER_H_
#define EXAMPLES_PEERCONNECTION_HEADLESS_COMMON_SIGNALING_HELPER_H_

#include <memory>
#include <string>

#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/scoped_refptr.h"
#include "json/value.h"

namespace signaling_helper {

extern const char kCandidateSdpMidName[];
extern const char kCandidateSdpMlineIndexName[];
extern const char kCandidateSdpName[];
extern const char kSessionDescriptionTypeName[];
extern const char kSessionDescriptionSdpName[];

Json::Value SerializeSdp(const webrtc::SessionDescriptionInterface* desc);
Json::Value SerializeIceCandidate(const webrtc::IceCandidate* candidate);

std::unique_ptr<webrtc::SessionDescriptionInterface> ParseSdp(
    const Json::Value& jmessage);
std::unique_ptr<webrtc::IceCandidate> ParseIceCandidate(
    const Json::Value& jmessage);

class DummySetSessionDescriptionObserver
    : public webrtc::SetSessionDescriptionObserver {
 public:
  static webrtc::scoped_refptr<DummySetSessionDescriptionObserver> Create() {
    return webrtc::make_ref_counted<DummySetSessionDescriptionObserver>();
  }
  void OnSuccess() override { RTC_LOG(LS_INFO) << __FUNCTION__; }
  void OnFailure(webrtc::RTCError error) override {
    RTC_LOG(LS_INFO) << __FUNCTION__ << " " << ToString(error.type()) << ": "
                     << error.message();
  }
};

}  // namespace signaling_helper

#endif  // EXAMPLES_PEERCONNECTION_HEADLESS_COMMON_SIGNALING_HELPER_H_
