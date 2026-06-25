/*
 *  Copyright 2024 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <regex>
#include <string>
#include <utility>

#include "absl/flags/flag.h"

// Suppress warnings for this demo code.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#include "absl/flags/parse.h"
#include "absl/memory/memory.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/create_modular_peer_connection_factory.h"
#include "api/enable_media.h"
#include "api/environment/environment.h"
#include "api/environment/environment_factory.h"
#include "api/field_trials.h"
#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/media_stream_interface.h"
#include "api/peer_connection_interface.h"
#include "api/rtc_error.h"
#include "api/rtp_receiver_interface.h"
#include "api/scoped_refptr.h"
#include "api/units/time_delta.h"
#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"
#include "api/video/video_source_interface.h"
#include "api/video_codecs/video_decoder_factory_template.h"
#include "api/video_codecs/video_decoder_factory_template_dav1d_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_libvpx_vp9_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_open_h264_adapter.h"
#include "api/video_codecs/video_encoder_factory_template.h"
#include "api/video_codecs/video_encoder_factory_template_libaom_av1_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp9_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_open_h264_adapter.h"
#include "examples/peerconnection/client/defaults.h"
#include "examples/peerconnection/client/flag_defs.h"
#include "examples/peerconnection/client/peer_connection_client.h"
#include "examples/peerconnection/headless_common/headless_socket_server.h"
#include "examples/peerconnection/headless_common/per_frame_loss_tracker.h"
#include "examples/peerconnection/headless_common/receiver_sink.h"
#include "examples/peerconnection/headless_common/sdl_renderer.h"
#include "examples/peerconnection/headless_common/signaling_helper.h"
#include "json/reader.h"
#include "json/writer.h"
#include "rtc_base/logging.h"
#include "rtc_base/ssl_adapter.h"
#include "rtc_base/strings/json.h"
#include "rtc_base/thread.h"

ABSL_FLAG(std::string, output_file, "received.y4m",
          "Path to write the received Y4M video.");
ABSL_FLAG(bool, play, false,
          "Play received video in real-time using an SDL window "
          "(ignores --output_file).");
ABSL_FLAG(int, width, 640,
          "Initial SDL window width. Auto-resizes to incoming video.");
ABSL_FLAG(int, height, 480,
          "Initial SDL window height. Auto-resizes to incoming video.");
ABSL_FLAG(bool, low_latency, false,
          "Enable low-latency streaming mode. "
          "Minimizes playout delay, reduces NACK wait time, limits decode queue.");
ABSL_FLAG(bool, reconnect, false,
          "Automatically reconnect to signaling server when disconnected.");

namespace {

class Receiver : public webrtc::PeerConnectionObserver,
                 public webrtc::CreateSessionDescriptionObserver,
                 public PeerConnectionClientObserver {
 public:
  Receiver(const webrtc::Environment& env,
           PeerConnectionClient* client,
           const std::string& output_file,
           bool play,
           bool low_latency,
           SdlVideoRenderer* sdl_renderer,
           bool reconnect)
      : env_(env), client_(client), sdl_renderer_(sdl_renderer),
        play_(play), low_latency_(low_latency), reconnect_(reconnect),
        output_file_(output_file) {
    if (!play_) {
      y4m_sink_ = std::make_unique<Y4mVideoSink>(output_file);
    }
    client_->RegisterObserver(this);
  }

  ~Receiver() override { DeletePeerConnection(); }

  bool should_retry() const { return reconnect_ && retry_; }
  void set_retry_flag() { retry_ = true; }

  void reset_for_retry() {
    DeletePeerConnection();
    retry_ = false;
  }

  // PeerConnectionClientObserver implementation.
  void OnSignedIn() override {
    fprintf(stderr, "[Receiver] Signed in. Waiting for sender to offer...\n");
    RTC_LOG(LS_INFO) << "Signed in. Waiting for sender to offer...";
  }

  void OnDisconnected() override {
    fprintf(stderr, "[Receiver] Disconnected from signaling server.\n");
    RTC_LOG(LS_INFO) << "Disconnected from signaling server.";
    if (reconnect_) {
      RTC_LOG(LS_INFO) << "Will retry connection in 3 seconds...";
      retry_ = true;
      webrtc::Thread::Current()->Quit();
    } else {
      DeletePeerConnection();
      webrtc::Thread::Current()->Quit();
    }
  }

  void OnPeerConnected(int id, const std::string& name) override {
    fprintf(stderr, "[Receiver] Peer connected: %s (id=%d)\n", name.c_str(), id);
    RTC_LOG(LS_INFO) << "Peer connected: " << name << " (id=" << id << ")";
  }

  void OnPeerDisconnected(int id) override {
    fprintf(stderr, "[Receiver] Peer disconnected: %d\n", id);
    RTC_LOG(LS_INFO) << "Peer disconnected: " << id;
    if (id == peer_id_) {
      DeletePeerConnection();
      webrtc::Thread::Current()->Quit();
    }
  }

  void OnMessageFromPeer(int peer_id, const std::string& message) override {
    fprintf(stderr, "[Receiver] Message from peer %d (len=%zu)\n", peer_id, message.size());
    RTC_DCHECK(!message.empty());

    if (!peer_connection_) {
      RTC_DCHECK(peer_id_ == -1);
      peer_id_ = peer_id;
      if (!InitializePeerConnection()) {
        RTC_LOG(LS_ERROR) << "Failed to initialize PeerConnection.";
        client_->SignOut();
        return;
      }
    } else if (peer_id != peer_id_) {
      RTC_LOG(LS_WARNING) << "Message from unknown peer.";
      return;
    }

    Json::CharReaderBuilder factory;
    std::unique_ptr<Json::CharReader> reader =
        absl::WrapUnique(factory.newCharReader());
    Json::Value jmessage;
    if (!reader->parse(message.data(), message.data() + message.length(),
                       &jmessage, nullptr)) {
      RTC_LOG(LS_WARNING) << "Received unknown message: " << message;
      return;
    }

    std::string type_str;
    webrtc::GetStringFromJsonObject(
        jmessage, signaling_helper::kSessionDescriptionTypeName, &type_str);

    if (!type_str.empty()) {
      auto desc = signaling_helper::ParseSdp(jmessage);
      if (!desc) {
        RTC_LOG(LS_ERROR) << "Failed to parse SDP.";
        return;
      }

      // Remove "nack" RTCP feedback from SDP only in low-latency mode.
      // This prevents the receiver from requesting NACK retransmissions,
      // so incomplete frames are sent directly to the decoder.
      std::string sdp = desc->ToString();

      if (low_latency_) {
        // Count nack lines before removal
        int nack_count = 0;
        for (size_t pos = 0; ; ) {
          pos = sdp.find("a=rtcp-fb:", pos);
          if (pos == std::string::npos) break;
          size_t nl = sdp.find('\n', pos);
          if (nl == std::string::npos) nl = sdp.size();
          std::string line = sdp.substr(pos, nl - pos);
          if (line.find(" nack") != std::string::npos) nack_count++;
          pos = nl + 1;
        }
        fprintf(stderr, "[Receiver] SDP has %d nack RTCP feedback lines\n", nack_count);

        sdp = std::regex_replace(sdp,
            std::regex("a=rtcp-fb:\\d+ nack[^\r\n]*\r?\n"), "");
        // Also remove RTX payload types (apt=XX)
        int rtx_count = 0;
        size_t rtx_pos = 0;
        while ((rtx_pos = sdp.find("rtx/90000", rtx_pos)) != std::string::npos) {
          rtx_count++;
          rtx_pos++;
        }
        fprintf(stderr, "[Receiver] Removed %d nack lines, %d rtx entries\n", nack_count, rtx_count);

        sdp = std::regex_replace(sdp,
            std::regex("a=rtpmap:\\d+ rtx/90000\r?\n"), "");
        sdp = std::regex_replace(sdp,
            std::regex("a=fmtp:\\d+ apt=\\d+\r?\n"), "");
      } else {
        fprintf(stderr, "[Receiver] NACK enabled (low_latency not set)\n");
      }

      // Log a snippet of modified SDP for verification
      size_t media_pos = sdp.find("m=video");
      if (media_pos != std::string::npos) {
        size_t end = sdp.find('\n', media_pos);
        if (end != std::string::npos) {
          std::string next_lines = sdp.substr(media_pos,
              std::min((size_t)200, sdp.size() - media_pos));
          RTC_LOG(LS_INFO) << "Modified SDP video section start: "
                           << next_lines;
        }
      }

      auto modified_desc = webrtc::CreateSessionDescription(desc->GetType(),
                                                            sdp);

      peer_connection_->SetRemoteDescription(
          signaling_helper::DummySetSessionDescriptionObserver::Create().get(),
          modified_desc.release());

      auto type_maybe = webrtc::SdpTypeFromString(type_str);
      if (type_maybe && *type_maybe == webrtc::SdpType::kOffer) {
        peer_connection_->CreateAnswer(
            this, webrtc::PeerConnectionInterface::RTCOfferAnswerOptions());
      }
    } else {
      auto candidate = signaling_helper::ParseIceCandidate(jmessage);
      if (candidate) {
        peer_connection_->AddIceCandidate(candidate.get());
      }
    }
  }

  void OnMessageSent(int err) override {}

  void OnServerConnectionFailure() override {
    RTC_LOG(LS_ERROR) << "Failed to connect to signaling server.";
    if (reconnect_) {
      RTC_LOG(LS_INFO) << "Will retry in 3 seconds...";
      retry_ = true;
    }
    webrtc::Thread::Current()->Quit();
  }

  // PeerConnectionObserver implementation.
  void OnSignalingChange(
      webrtc::PeerConnectionInterface::SignalingState new_state) override {
    RTC_LOG(LS_INFO) << "Signaling state: " << new_state;
  }

  void OnAddTrack(
      webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
      const std::vector<webrtc::scoped_refptr<webrtc::MediaStreamInterface>>&
          streams) override {
    RTC_LOG(LS_INFO) << "Track received: " << receiver->id();
    auto track = receiver->track();
    if (track &&
        track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind) {
      auto* video_track =
          static_cast<webrtc::VideoTrackInterface*>(track.get());
      if (play_) {
        if (sdl_renderer_ && sdl_renderer_->initialized()) {
          video_track->AddOrUpdateSink(
              static_cast<webrtc::VideoSinkInterface<webrtc::VideoFrame>*>(
                  sdl_renderer_),
              webrtc::VideoSinkWants());
          RTC_LOG(LS_INFO) << "SdlVideoRenderer registered on incoming video track.";
        } else {
          RTC_LOG(LS_ERROR) << "SDL renderer not initialized, cannot display video.";
        }
      } else {
        video_track->AddOrUpdateSink(y4m_sink_.get(), webrtc::VideoSinkWants());
        RTC_LOG(LS_INFO) << "Y4mVideoSink registered on incoming video track.";
      }
    }
  }

  void OnRemoveTrack(
      webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override {
    RTC_LOG(LS_INFO) << "Track removed.";
  }

  void OnDataChannel(
      webrtc::scoped_refptr<webrtc::DataChannelInterface> channel) override {}

  void OnRenegotiationNeeded() override {}

  void OnIceConnectionChange(
      webrtc::PeerConnectionInterface::IceConnectionState new_state) override {
    RTC_LOG(LS_INFO) << "ICE connection state: " << new_state;
    if (new_state ==
        webrtc::PeerConnectionInterface::kIceConnectionDisconnected) {
      RTC_LOG(LS_INFO) << "ICE disconnected. Exiting.";
      DeletePeerConnection();
      webrtc::Thread::Current()->Quit();
    }
  }

  void OnIceConnectionReceivingChange(bool receiving) override {}

  void OnIceGatheringChange(
      webrtc::PeerConnectionInterface::IceGatheringState new_state) override {}

  void OnIceCandidate(const webrtc::IceCandidate* candidate) override;

  void OnIceCandidateRemoved(const webrtc::IceCandidate* candidate) override {}

  // CreateSessionDescriptionObserver implementation.
  void OnSuccess(webrtc::SessionDescriptionInterface* desc) override;

  void OnFailure(webrtc::RTCError error) override {
    RTC_LOG(LS_ERROR) << ToString(error.type()) << ": " << error.message();
  }

 private:
  bool InitializePeerConnection() {
    webrtc::PeerConnectionFactoryDependencies deps;
    deps.signaling_thread = webrtc::Thread::Current();
    deps.env = env_;
    deps.audio_encoder_factory = webrtc::CreateBuiltinAudioEncoderFactory();
    deps.audio_decoder_factory = webrtc::CreateBuiltinAudioDecoderFactory();
    deps.video_encoder_factory =
        std::make_unique<webrtc::VideoEncoderFactoryTemplate<
            webrtc::LibvpxVp8EncoderTemplateAdapter,
            webrtc::LibvpxVp9EncoderTemplateAdapter,
            webrtc::OpenH264EncoderTemplateAdapter,
            webrtc::LibaomAv1EncoderTemplateAdapter>>();
    deps.video_decoder_factory =
        std::make_unique<webrtc::VideoDecoderFactoryTemplate<
            webrtc::LibvpxVp8DecoderTemplateAdapter,
            webrtc::LibvpxVp9DecoderTemplateAdapter,
            webrtc::OpenH264DecoderTemplateAdapter,
            webrtc::Dav1dDecoderTemplateAdapter>>();
    webrtc::EnableMedia(deps);
    peer_connection_factory_ =
        webrtc::CreateModularPeerConnectionFactory(std::move(deps));

    if (!peer_connection_factory_) {
      RTC_LOG(LS_ERROR) << "Failed to create PeerConnectionFactory.";
      return false;
    }

    webrtc::PeerConnectionInterface::RTCConfiguration config;
    config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
    webrtc::PeerConnectionInterface::IceServer server;
    server.uri = GetPeerConnectionString();
    server.username = GetTurnUserName();
    server.password = GetTurnPassword();
    config.servers.push_back(server);

    webrtc::PeerConnectionDependencies pc_dependencies(this);
    auto error_or_peer_connection =
        peer_connection_factory_->CreatePeerConnectionOrError(
            config, std::move(pc_dependencies));
    if (!error_or_peer_connection.ok()) {
      RTC_LOG(LS_ERROR) << "Failed to create PeerConnection.";
      return false;
    }
    peer_connection_ = std::move(error_or_peer_connection.value());
    return true;
  }

  void DeletePeerConnection() {
    peer_connection_ = nullptr;
    peer_connection_factory_ = nullptr;
    peer_id_ = -1;
  }

  void SendMessage(const std::string& json_object) {
    if (peer_id_ == -1)
      return;
    while (client_->IsSendingMessage()) {
      webrtc::Thread::Current()->ProcessMessages(10);
    }
    client_->SendToPeer(peer_id_, json_object);
  }

  int peer_id_ = -1;
  const webrtc::Environment& env_;
  PeerConnectionClient* client_;
  webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection_;
  webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>
      peer_connection_factory_;
  std::unique_ptr<Y4mVideoSink> y4m_sink_;
  SdlVideoRenderer* sdl_renderer_ = nullptr;  // Not owned, created in main().
  bool play_ = false;
  bool low_latency_ = false;
  bool reconnect_ = false;
  bool retry_ = false;
  std::string output_file_;
};

void Receiver::OnIceCandidate(const webrtc::IceCandidate* candidate) {
  Json::Value jmessage = signaling_helper::SerializeIceCandidate(candidate);
  Json::StreamWriterBuilder factory;
  SendMessage(Json::writeString(factory, jmessage));
}

void Receiver::OnSuccess(webrtc::SessionDescriptionInterface* desc) {
  peer_connection_->SetLocalDescription(
      signaling_helper::DummySetSessionDescriptionObserver::Create().get(),
      desc);

  Json::Value jmessage = signaling_helper::SerializeSdp(desc);
  Json::StreamWriterBuilder factory;
  SendMessage(Json::writeString(factory, jmessage));
}

}  // namespace

int main(int argc, char* argv[]) {
  absl::ParseCommandLine(argc, argv);

  // Build field trials string, appending low-latency trials if requested.
  std::string field_trials = absl::GetFlag(FLAGS_force_fieldtrials);
  if (absl::GetFlag(FLAGS_low_latency)) {
    if (!field_trials.empty())
      field_trials += " ";
    field_trials +=
        "WebRTC-ZeroPlayoutDelay/enabled,min_pacing:8ms,max_decode_queue_size:3/";
  }

  webrtc::Environment env =
      webrtc::CreateEnvironment(std::make_unique<webrtc::FieldTrials>(
          field_trials));

  int port = absl::GetFlag(FLAGS_port);
  if (port < 1 || port > 65535) {
    printf("Error: %i is not a valid port.\n", port);
    return 1;
  }

  std::string server = absl::GetFlag(FLAGS_server);
  if (server.empty()) {
    printf("Error: --server must be specified.\n");
    return 1;
  }

  std::string output_file = absl::GetFlag(FLAGS_output_file);
  bool play = absl::GetFlag(FLAGS_play);
  bool low_latency = absl::GetFlag(FLAGS_low_latency);

  std::string peer_name = absl::GetFlag(FLAGS_name);
  if (peer_name.empty()) {
    peer_name = GetPeerName();
  }

  bool reconnect = absl::GetFlag(FLAGS_reconnect);

  webrtc::InitializeSSL();

  HeadlessSocketServer socket_server;
  webrtc::AutoSocketServerThread thread(&socket_server);

  // Create SDL renderer on the main thread (before thread.Run()).
  SdlVideoRenderer* sdl_renderer = nullptr;
  std::unique_ptr<SdlVideoRenderer> sdl_renderer_owner;
  if (play) {
    int win_width = absl::GetFlag(FLAGS_width);
    int win_height = absl::GetFlag(FLAGS_height);
    sdl_renderer_owner = std::make_unique<SdlVideoRenderer>(
        "WebRTC Receiver", win_width, win_height);
    sdl_renderer = sdl_renderer_owner.get();
  }

  // Create per-frame loss tracker at RTP layer.
  auto per_frame_loss_tracker = std::make_unique<PerFrameLossTracker>(nullptr);
  g_per_frame_loss_tracker.store(per_frame_loss_tracker.get(),
                                  std::memory_order_relaxed);

  // Main loop with optional reconnect.
  while (true) {
    PeerConnectionClient client;
    auto receiver =
        webrtc::make_ref_counted<Receiver>(env, &client, output_file, play,
                                           low_latency, sdl_renderer, reconnect);
    client.Connect(server, port, peer_name);

    // Run the message loop, pumping SDL rendering between messages.
    if (play && sdl_renderer) {
      SetupSignalHandler();

      webrtc::Thread* main_thread = webrtc::Thread::Current();
      while (main_thread->ProcessMessages(16)) {
        sdl_renderer->RenderPendingFrames();
      }
      RTC_LOG(LS_INFO) << "Exited main loop.";
    } else {
      thread.Run();
    }

    if (!reconnect || !receiver->should_retry()) {
      break;  // Normal exit or reconnect disabled.
    }

    RTC_LOG(LS_INFO) << "Reconnecting in 3 seconds...";
    receiver->reset_for_retry();
    // Drain socket server events from failed connection.
    webrtc::Thread::Current()->ProcessMessages(3000);
  }

  // Clean up global pointer before tracker is destroyed.
  g_per_frame_loss_tracker.store(nullptr, std::memory_order_relaxed);
  per_frame_loss_tracker.reset();

  webrtc::CleanupSSL();
  return 0;
}

#pragma clang diagnostic pop
