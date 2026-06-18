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
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"

// Suppress warnings for this demo code.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#include "absl/flags/parse.h"
#include "absl/memory/memory.h"
#include "api/audio/audio_device.h"
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
#include "api/rtp_sender_interface.h"
#include "api/scoped_refptr.h"
#include "api/task_queue/task_queue_factory.h"
#include "api/test/create_frame_generator.h"
#include "api/transport/bitrate_settings.h"
#include "api/units/time_delta.h"
#include "api/video/video_frame.h"
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
#include "examples/peerconnection/headless_common/signaling_helper.h"
#include "json/reader.h"
#include "json/writer.h"
#include "modules/video_capture/video_capture.h"
#include "modules/video_capture/video_capture_factory.h"
#include "pc/video_track_source.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/ssl_adapter.h"
#include "rtc_base/strings/json.h"
#include "rtc_base/thread.h"
#include "test/frame_generator_capturer.h"
#include "test/platform_video_capturer.h"
#include "test/test_video_capturer.h"
#include "test/testsupport/y4m_frame_generator.h"

ABSL_FLAG(int, max_bitrate, 1000,
          "Max video bitrate in kbps. Reduces if streaming 1-2s then "
          "freezing/garbled.");
ABSL_FLAG(int, width, 640,
          "Video capture width. Used only with camera/synthetic source.");
ABSL_FLAG(int, height, 480,
          "Video capture height. Used only with camera/synthetic source.");
ABSL_FLAG(bool, low_latency, false,
          "Enable low-latency streaming mode. "
          "Reduces NACK delay and limits retransmissions.");
ABSL_FLAG(double, simulate_loss, 0.0,
          "Simulate UDP packet loss rate (0.0 to 1.0). "
          "E.g., 0.05 = 5%% packet loss.");
ABSL_FLAG(std::string, periodic_loss, "",
          "Periodic packet loss pattern: '<interval_ms>:<drop_count>'. "
          "E.g., '2000:3' = drop 3 packets every 2 seconds.");
ABSL_FLAG(std::string, burst_loss, "",
          "Burst packet loss: '<start_s>:<duration_s>:<loss_pct>'. "
          "E.g., '15:0.5:0.5' = at 15s, lose 50%% for 0.5s.");

namespace {

using webrtc::test::TestVideoCapturer;

std::unique_ptr<TestVideoCapturer> CreateCapturer(
    webrtc::TaskQueueFactory& task_queue_factory) {
  std::string video_file = absl::GetFlag(FLAGS_video_file);
  if (!video_file.empty()) {
    RTC_LOG(LS_INFO) << "Using video file: " << video_file;
    auto frame_generator = std::make_unique<webrtc::test::Y4mFrameGenerator>(
        video_file, webrtc::test::Y4mFrameGenerator::RepeatMode::kLoop);
    auto resolution = frame_generator->GetResolution();
    int fps = frame_generator->fps().value_or(30);
    RTC_LOG(LS_INFO) << "Video file resolution: " << resolution.width << "x"
                     << resolution.height << ", fps: " << fps;
    return std::make_unique<webrtc::test::FrameGeneratorCapturer>(
        webrtc::Clock::GetRealTimeClock(), std::move(frame_generator), fps,
        task_queue_factory);
  }
  const size_t kWidth = absl::GetFlag(FLAGS_width);
  const size_t kHeight = absl::GetFlag(FLAGS_height);
  const size_t kFps = 30;
  std::unique_ptr<webrtc::VideoCaptureModule::DeviceInfo> info(
      webrtc::VideoCaptureFactory::CreateDeviceInfo());
  if (info) {
    int num_devices = info->NumberOfDevices();
    for (int i = 0; i < num_devices; ++i) {
      std::unique_ptr<TestVideoCapturer> capturer =
          webrtc::test::CreateVideoCapturer(kWidth, kHeight, kFps, i);
      if (capturer) {
        return capturer;
      }
    }
  }
  RTC_LOG(LS_WARNING)
      << "No video capture device found; using synthetic video.";
  auto frame_generator = webrtc::test::CreateSquareFrameGenerator(
      kWidth, kHeight, std::nullopt, std::nullopt);
  return std::make_unique<webrtc::test::FrameGeneratorCapturer>(
      webrtc::Clock::GetRealTimeClock(), std::move(frame_generator), kFps,
      task_queue_factory);
}

class CapturerTrackSource : public webrtc::VideoTrackSource {
 public:
  static webrtc::scoped_refptr<CapturerTrackSource> Create(
      webrtc::TaskQueueFactory& task_queue_factory) {
    std::unique_ptr<TestVideoCapturer> capturer =
        CreateCapturer(task_queue_factory);
    if (capturer) {
      capturer->Start();
      return webrtc::make_ref_counted<CapturerTrackSource>(std::move(capturer));
    }
    return nullptr;
  }

 protected:
  explicit CapturerTrackSource(std::unique_ptr<TestVideoCapturer> capturer)
      : VideoTrackSource(/*remote=*/false), capturer_(std::move(capturer)) {}

  ~CapturerTrackSource() override = default;

 private:
  webrtc::VideoSourceInterface<webrtc::VideoFrame>* source() override {
    return capturer_.get();
  }
  std::unique_ptr<TestVideoCapturer> capturer_;
};

class Sender : public webrtc::PeerConnectionObserver,
               public webrtc::CreateSessionDescriptionObserver,
               public PeerConnectionClientObserver {
 public:
  Sender(const webrtc::Environment& env,
         PeerConnectionClient* client)
      : env_(env), client_(client) {
    client_->RegisterObserver(this);
  }

  ~Sender() override { DeletePeerConnection(); }

  // PeerConnectionClientObserver implementation.
  void OnPeerConnected(int id, const std::string& name) override {
    fprintf(stderr, "[Sender] Peer connected: %s (id=%d)\n", name.c_str(), id);
    RTC_LOG(LS_INFO) << "Peer connected: " << name << " (id=" << id << ")";
    // Track all peers that look like receivers (name starts with "rx_").
    if (name.rfind("rx_", 0) == 0) {
      // Always pick the newest receiver (highest ID) to avoid stale peers.
      if (peer_id_ == -1 || id > peer_id_) {
        peer_id_ = id;
        RTC_LOG(LS_INFO) << "Selected receiver: " << name << " (id=" << id << ")";
      }
      candidate_peers_[id] = name;
      // If already signed in, start the call immediately to the newest receiver.
      if (peer_connection_) {
        // Already in a call, but a newer receiver appeared. Restart.
        RTC_LOG(LS_INFO) << "Newer receiver appeared, restarting call.";
        DeletePeerConnection();
        StartCall();
      }
    }
  }

  void OnSignedIn() override {
    fprintf(stderr, "[Sender] Signed in. Waiting for receiver to connect...\n");
    RTC_LOG(LS_INFO) << "Signed in. Waiting for receiver to connect...";
    // If we already have a receiver candidate from the sign-in response, start the call.
    if (peer_id_ != -1) {
      RTC_LOG(LS_INFO) << "Starting call to receiver id=" << peer_id_;
      StartCall();
    }
  }

  void OnDisconnected() override {
    fprintf(stderr, "[Sender] Disconnected from signaling server.\n");
    RTC_LOG(LS_INFO) << "Disconnected from signaling server.";
    DeletePeerConnection();
    webrtc::Thread::Current()->Quit();
  }

  void OnPeerDisconnected(int id) override {
    fprintf(stderr, "[Sender] Peer disconnected: %d\n", id);
    RTC_LOG(LS_INFO) << "Peer disconnected: " << id;
    if (id == peer_id_) {
      RTC_LOG(LS_INFO) << "Our peer disconnected. Trying next candidate.";
      peer_id_ = -1;
      DeletePeerConnection();
      // Try to connect to the next available receiver candidate.
      if (!candidate_peers_.empty()) {
        auto it = candidate_peers_.begin();
        peer_id_ = it->first;
        RTC_LOG(LS_INFO) << "Connecting to candidate receiver: " << it->second;
        StartCall();
        candidate_peers_.erase(it);
      }
    } else {
      // Remove from candidates if present.
      candidate_peers_.erase(id);
    }
  }

  void OnMessageFromPeer(int peer_id, const std::string& message) override {
    fprintf(stderr, "[Sender] Message from peer %d (len=%zu)\n", peer_id, message.size());
    RTC_DCHECK(peer_id_ == peer_id || peer_id_ == -1);
    RTC_DCHECK(!message.empty());

    if (!peer_connection_) {
      peer_id_ = peer_id;
      if (!InitializePeerConnection()) {
        RTC_LOG(LS_ERROR) << "Failed to initialize PeerConnection.";
        client_->SignOut();
        return;
      }
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
      RTC_LOG(LS_INFO) << "Received SDP: " << type_str;
      auto desc = signaling_helper::ParseSdp(jmessage);
      if (!desc) {
        RTC_LOG(LS_ERROR) << "Failed to parse SDP.";
        return;
      }
      peer_connection_->SetRemoteDescription(
          signaling_helper::DummySetSessionDescriptionObserver::Create().get(),
          desc.release());
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
          streams) override {}

  void OnRemoveTrack(
      webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override {}

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
  void StartCall() {
    if (!InitializePeerConnection()) {
      RTC_LOG(LS_ERROR) << "Failed to initialize PeerConnection.";
      client_->SignOut();
      return;
    }
    // Limit bitrate to avoid congestion that causes garbled/frozen video.
    webrtc::BitrateSettings bitrate;
    int max_kbps = absl::GetFlag(FLAGS_max_bitrate);
    bitrate.max_bitrate_bps = max_kbps * 1000;
    peer_connection_->SetBitrate(bitrate);
    AddTracks();
    peer_connection_->CreateOffer(
        this, webrtc::PeerConnectionInterface::RTCOfferAnswerOptions());
  }

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
    local_video_source_ = nullptr;
    peer_id_ = -1;
  }

  void AddTracks() {
    local_video_source_ = CapturerTrackSource::Create(env_.task_queue_factory());
    if (local_video_source_) {
      auto video_track = peer_connection_factory_->CreateVideoTrack(
          local_video_source_, kVideoLabel);
      auto result = peer_connection_->AddTrack(video_track, {kStreamId});
      if (!result.ok()) {
        RTC_LOG(LS_ERROR) << "Failed to add video track: "
                          << result.error().message();
      } else {
        RTC_LOG(LS_INFO) << "Video track added.";
      }
    } else {
      RTC_LOG(LS_WARNING) << "No video source available.";
    }
  }

  void SendMessage(const std::string& json_object) {
    if (peer_id_ == -1)
      return;
    // Back-to-back OnIceCandidate callbacks can arrive while the control
    // socket is still busy from a previous send.  Process I/O events to let
    // it complete before sending the next message.
    while (client_->IsSendingMessage()) {
      webrtc::Thread::Current()->ProcessMessages(10);
    }
    client_->SendToPeer(peer_id_, json_object);
  }

  int peer_id_ = -1;
  std::map<int, std::string> candidate_peers_;
  const webrtc::Environment& env_;
  PeerConnectionClient* client_;
  webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection_;
  webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>
      peer_connection_factory_;
  webrtc::scoped_refptr<webrtc::VideoTrackSourceInterface>
      local_video_source_;
};

void Sender::OnIceCandidate(const webrtc::IceCandidate* candidate) {
  Json::Value jmessage = signaling_helper::SerializeIceCandidate(candidate);
  Json::StreamWriterBuilder factory;
  SendMessage(Json::writeString(factory, jmessage));
}

void Sender::OnSuccess(webrtc::SessionDescriptionInterface* desc) {
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
        "WebRTC-SendNackDelayMs/delay:0/WebRTC-JitterEstimatorConfig/nack_limit:3/";
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

  std::string video_file = absl::GetFlag(FLAGS_video_file);
  if (video_file.empty()) {
    printf("Error: --video_file must be specified.\n");
    return 1;
  }

  std::string peer_name = absl::GetFlag(FLAGS_name);
  if (peer_name.empty()) {
    peer_name = GetPeerName();
  }

  SetupSignalHandler();

  double loss_rate = absl::GetFlag(FLAGS_simulate_loss);
  if (loss_rate > 0.0) {
    webrtc::SetPacketLossRate(loss_rate);
    printf("Packet loss simulation enabled: %.1f%% random loss\n",
           loss_rate * 100);
  }

  std::string periodic = absl::GetFlag(FLAGS_periodic_loss);
  if (!periodic.empty()) {
    size_t colon = periodic.find(':');
    if (colon != std::string::npos) {
      int64_t interval_ms = std::stoll(periodic.substr(0, colon));
      int drop_count = std::stoi(periodic.substr(colon + 1));
      webrtc::SetPeriodicLoss(interval_ms * 1000, drop_count);
      printf("Periodic packet loss: drop %d packets every %lld ms\n",
             drop_count, (long long)interval_ms);
    } else {
      printf("Invalid periodic_loss format, expected '<ms>:<count>'\n");
    }
  }

  std::string burst = absl::GetFlag(FLAGS_burst_loss);
  if (!burst.empty()) {
    size_t p1 = burst.find(':');
    size_t p2 = burst.find(':', p1 + 1);
    if (p1 != std::string::npos && p2 != std::string::npos) {
      double start_s = std::stod(burst.substr(0, p1));
      double duration_s = std::stod(burst.substr(p1 + 1, p2 - p1 - 1));
      double loss_pct = std::stod(burst.substr(p2 + 1));
      webrtc::SetBurstLoss(
          static_cast<int64_t>(start_s * 1000000),
          static_cast<int64_t>(duration_s * 1000000),
          loss_pct);
      printf("Burst packet loss: at %.1fs, lose %.0f%% for %.3fs\n",
             start_s, loss_pct * 100, duration_s);
    } else {
      printf("Invalid burst_loss format, expected '<start_s>:<duration_s>:<pct>'\n");
    }
  }

  webrtc::InitializeSSL();

  HeadlessSocketServer socket_server;
  webrtc::AutoSocketServerThread thread(&socket_server);

  PeerConnectionClient client;
  auto sender = webrtc::make_ref_counted<Sender>(env, &client);
  client.Connect(server, port, peer_name);

  thread.Run();

  webrtc::CleanupSSL();
  return 0;
}

#pragma clang diagnostic pop
