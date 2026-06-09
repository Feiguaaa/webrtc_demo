/*
 *  Copyright 2024 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "examples/peerconnection/headless_common/signaling_helper.h"

#include <memory>
#include <string>

#include "absl/memory/memory.h"
#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/scoped_refptr.h"
#include "json/reader.h"
#include "json/value.h"
#include "json/writer.h"
#include "rtc_base/logging.h"
#include "rtc_base/strings/json.h"

namespace signaling_helper {

const char kCandidateSdpMidName[] = "sdpMid";
const char kCandidateSdpMlineIndexName[] = "sdpMLineIndex";
const char kCandidateSdpName[] = "candidate";
const char kSessionDescriptionTypeName[] = "type";
const char kSessionDescriptionSdpName[] = "sdp";

Json::Value SerializeSdp(const webrtc::SessionDescriptionInterface* desc) {
  std::string sdp;
  desc->ToString(&sdp);

  Json::Value jmessage;
  jmessage[kSessionDescriptionTypeName] =
      webrtc::SdpTypeToString(desc->GetType());
  jmessage[kSessionDescriptionSdpName] = sdp;
  return jmessage;
}

Json::Value SerializeIceCandidate(const webrtc::IceCandidate* candidate) {
  Json::Value jmessage;
  jmessage[kCandidateSdpMidName] = candidate->sdp_mid();
  jmessage[kCandidateSdpMlineIndexName] = candidate->sdp_mline_index();
  jmessage[kCandidateSdpName] = candidate->ToString();
  return jmessage;
}

std::unique_ptr<webrtc::SessionDescriptionInterface> ParseSdp(
    const Json::Value& jmessage) {
  std::string type_str;
  std::string sdp;
  if (!webrtc::GetStringFromJsonObject(jmessage, kSessionDescriptionTypeName,
                                       &type_str) ||
      !webrtc::GetStringFromJsonObject(jmessage, kSessionDescriptionSdpName,
                                       &sdp)) {
    RTC_LOG(LS_WARNING) << "Can't parse session description message.";
    return nullptr;
  }
  auto type_maybe = webrtc::SdpTypeFromString(type_str);
  if (!type_maybe) {
    RTC_LOG(LS_ERROR) << "Unknown SDP type: " << type_str;
    return nullptr;
  }
  webrtc::SdpParseError error;
  auto session_description =
      webrtc::CreateSessionDescription(*type_maybe, sdp, &error);
  if (!session_description) {
    RTC_LOG(LS_WARNING)
        << "Can't parse received session description message. "
           "SdpParseError was: "
        << error.description;
    return nullptr;
  }
  return session_description;
}

std::unique_ptr<webrtc::IceCandidate> ParseIceCandidate(
    const Json::Value& jmessage) {
  std::string sdp_mid;
  int sdp_mlineindex = 0;
  std::string sdp;
  if (!webrtc::GetStringFromJsonObject(jmessage, kCandidateSdpMidName,
                                       &sdp_mid) ||
      !webrtc::GetIntFromJsonObject(jmessage, kCandidateSdpMlineIndexName,
                                    &sdp_mlineindex) ||
      !webrtc::GetStringFromJsonObject(jmessage, kCandidateSdpName, &sdp)) {
    RTC_LOG(LS_WARNING) << "Can't parse received candidate message.";
    return nullptr;
  }
  webrtc::SdpParseError error;
  std::unique_ptr<webrtc::IceCandidate> candidate(
      webrtc::CreateIceCandidate(sdp_mid, sdp_mlineindex, sdp, &error));
  if (!candidate) {
    RTC_LOG(LS_WARNING) << "Can't parse received candidate message. "
                           "SdpParseError was: "
                        << error.description;
    return nullptr;
  }
  return candidate;
}

}  // namespace signaling_helper
