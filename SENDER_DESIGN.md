# Sender 端 RTP Header 扩展修改规范

## 需要修改的文件

### 1. `modules/rtp_rtcp/include/rtp_rtcp_defines.h`

添加 URI 常量声明：

```cpp
namespace RtpExtension {
  constexpr char kFramePacketInfoUri[] =
      "http://www.webrtc.org/experiments/rtp-hdrext/frame_packet_info";
}
```

### 2. `modules/rtp_rtcp/source/rtp_header_extensions.h`

添加扩展序列化类：

```cpp
struct FramePacketInfo {
  uint16_t total_packets;    // 10 bits, 0-1023
  uint16_t packet_index;     // 6 bits, 0-63
  uint16_t frame_sequence;   // 10 bits, 0-1023, wraps at 1024
};

class FramePacketInfoExtension {
 public:
  static constexpr size_t kSize = 4;
  static bool Parse(rtc::ArrayView<const uint8_t> data, FramePacketInfo* info);
  static size_t Write(rtc::ArrayView<uint8_t> data, const FramePacketInfo& info);
};

// kId = 5 (matches SDP extmap:5)
class FramePacketInfoExtension {
 public:
  static constexpr int kId = 5;
  static constexpr size_t kSize = 4;
  static bool Parse(rtc::ArrayView<const uint8_t> data, FramePacketInfo* info) {
    if (data.size() < 4) return false;
    uint16_t w0 = (data[0] << 8) | data[1];
    uint16_t w1 = (data[2] << 8) | data[3];
    info->total_packets = (w0 >> 6) & 0x3FF;
    info->packet_index = ((w0 & 0x3F) << 4) | ((w1 >> 12) & 0x0F);
    info->frame_sequence = w1 & 0x3FF;
    return true;
  }
  static size_t Write(rtc::ArrayView<uint8_t> data, const FramePacketInfo& info) {
    if (data.size() < 4) return 0;
    uint16_t w0 = (info.total_packets << 6) | (info.packet_index >> 4);
    uint16_t w1 = (info.packet_index << 12) | (info.frame_sequence << 6);
    data[0] = (w0 >> 8) & 0xFF;
    data[1] = w0 & 0xFF;
    data[2] = (w1 >> 8) & 0xFF;
    data[3] = w1 & 0xFF;
    return 4;
  }
};
```

### 3. `modules/rtp_rtcp/source/rtp_header_extension_map.cc`

注册扩展类型：

```cpp
#include "modules/rtp_rtcp/source/rtp_header_extensions.h"

// 在 GetExtensionHeaderSize() 或等价注册函数中添加：
if (absl::StartsWith(uri, RtpExtension::kFramePacketInfoUri)) {
  return FramePacketInfoExtension::kSize;
}
```

### 4. `api/rtp_parameters.h` 和 `api/rtp_parameters.cc`

添加结构体定义（如果还没有的话）：

```cpp
struct FramePacketInfo {
  uint16_t total_packets;
  uint16_t packet_index;
  uint16_t frame_sequence;
};
```

### 5. `media/engine/webrtc_video_engine.cc`

在 `GetRtpHeaderExtensions()` 函数中加入 SDP 协商：

```cpp
// 在视频扩展列表中追加：
result.emplace_back(RtpExtension::kFramePacketInfoUri, 5,
                    RtpTransceiverDirection::kSendRecv);
```

### 6. `modules/rtp_rtcp/source/rtp_sender_video.cc`

在 `SendVideo()` 中为每个 RTP 包附加扩展：

```cpp
// 类成员变量（帧级别状态）
uint16_t frame_sequence_counter_ = 0;

// 在 SendVideo() 中，发送每个 RTP 包前：
if (num_packets <= 1023) {
  FramePacketInfo frame_info;
  frame_info.total_packets = static_cast<uint16_t>(num_packets);
  frame_info.packet_index = static_cast<uint16_t>(packet_index);
  frame_info.frame_sequence = frame_sequence_counter_;
  bool ok = packet->SetExtension<FramePacketInfoExtension>(frame_info);
  if (!ok) {
    RTC_LOG(LS_WARNING) << "Failed to set FramePacketInfoExtension";
  }
}

// 整帧发送完毕后（最后一个 packet 发送后）：
frame_sequence_counter_++;
if (frame_sequence_counter_ >= 1024) {
  frame_sequence_counter_ = 0;
}
```

## RTP 包扩展格式（线路格式）

每个视频 RTP 包的 Header Extension 部分：

```
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| 0xBE | 0xDE  |   Header Extension Count                       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  ID=5 |LEN=3|  Byte 0  |  Byte 1  |  Byte 2  |  Byte 3        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  ... 其他扩展（abs-send-time, transport-cc 等）...             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

4 字节数据（大端序）：

```
Byte 0: total_packets[9:2]         (高 8 位)
Byte 1: (total_packets & 0x3F) << 2 | (packet_index >> 4)
Byte 2: frame_sequence[9:2]        (高 8 位)
Byte 3: (frame_sequence & 0x3F) << 2 | 0x00  (padding=0)
```

## 字段说明

| 字段 | 位宽 | 范围 | 说明 |
|------|------|------|------|
| `total_packets` | 10 bits | 0-1023 | 当前视频帧的 RTP 包总数 |
| `packet_index` | 6 bits | 0-63 | 当前包在该帧中的索引（从 0 开始） |
| `frame_sequence` | 10 bits | 0-1023 | 帧序列号，每发完一帧 +1，1024 回绕 |

## SDP 中的表现

SDP Offer 的 `m=video` 段中会出现：

```
a=extmap:5 http://www.webrtc.org/experiments/rtp-hdrext/frame_packet_info
```

## 验证方法

Receiver 端前 3 个包会打印：

```
[RtpLossTracker] pkt#1: ssrc=xxx seq=xxx pt=103 size=xxx
[RtpLossTracker]   One-byte ext: 0xBEDE count=N
[RtpLossTracker]     ext ID=5 len=4 bytes=XXXXXXXX
[RtpLossTracker]   => FramePacketInfo: total=X idx=X frame_seq=X
```

看到 `First extension parsed` 即成功，CSV 写入 `output/rtp_session_*.csv`。

---

## Linux/Windows 平台适配说明

### 与原设计的差异

原设计基于 macOS (New WebRTC API, `std::span`, `RtpHeaderExtensionId` 强类型)，Linux 平台的 WebRTC 版本较旧，以下为适配修改：

### 新增文件

| 文件 | 说明 |
|------|------|
| `modules/rtp_rtcp/source/rtp_video_header.h` | 定义 `FramePacketInfo` 和 `EncoderTargetBitrate` 结构体 |
| `api/rtp_parameters.h` | 定义 `kFramePacketInfoUri` / `kEncoderTargetBitrateUri` URI 常量 |
| `api/rtp_header_extension_id.h` | （未使用）旧版不支持 `RtpHeaderExtensionId` 强类型，改用 `int` |

### 修改文件

| 文件 | 修改内容 |
|------|----------|
| `modules/rtp_rtcp/include/rtp_rtcp_defines.h` | 添加 `kRtpExtensionEncoderTargetBitrate` / `kRtpExtensionFramePacketInfo` 枚举值 |
| `modules/rtp_rtcp/source/rtp_header_extensions.h` | 添加 `EncoderTargetBitrateExtension`(2B) + `FramePacketInfoExtension`(4B) 序列化类 |
| `modules/rtp_rtcp/source/rtp_header_extension_map.cc` | 注册两个新扩展到 `kExtensions` 数组 |
| `modules/rtp_rtcp/source/rtp_sender_video.cc` | 每包 `SetExtension<FramePacketInfoExtension>` + 首包 `SetExtension<EncoderTargetBitrateExtension>` |
| `modules/rtp_rtcp/source/rtp_sender_video.h` | 成员变量 `frame_sequence_counter_` (10-bit, 1024 回绕) |
| `media/engine/webrtc_video_engine.cc` | `GetRtpHeaderExtensions()` 中注册 URI → SDP `extmap:4` / `extmap:5` |
| `api/rtp_parameters.h` | 添加 URI 常量、修改 `preferred_id` 为 `std::optional<int>` 兼容旧版 |

### 扩展对照表

| 扩展 | SDP ID | URI | 数据长度 | 写入时机 | 值类型 |
|------|--------|-----|----------|----------|--------|
| `FramePacketInfoExtension` | 5 | `.../frame_packet_info` | 4 bytes | 每帧的每个包 | `FramePacketInfo` |
| `EncoderTargetBitrateExtension` | 4 | `.../encoder_target_bitrate` | 2 bytes | 仅首包 | `EncoderTargetBitrate` |

### 完整字节布局

#### FramePacketInfoExtension (ID=5, 4 bytes)

```
Byte 0: total_packets[9:2]         → (total_packets >> 2) & 0xFF
Byte 1: (total_packets & 0x3F) << 2 | (packet_index >> 4)
Byte 2: frame_sequence[9:2]        → (frame_sequence >> 2) & 0xFF
Byte 3: (frame_sequence & 0x3F) << 2
```

#### EncoderTargetBitrateExtension (ID=4, 2 bytes)

```
Byte 0-1: target_bitrate_kbps      → uint16_t big-endian
```

### API 差异说明

| 原设计 (macOS) | Linux/Windows 适配 | 原因 |
|----------------|-------------------|------|
| `std::span<const uint8_t>` | `ArrayView<const uint8_t>` | 旧版 WebRTC 无 `std::span` |
| `RtpHeaderExtensionId` 强类型 | `int` | 旧版无此类型 |
| `#include <span>` | `#include "api/array_view.h"` | 对应头文件差异 |
| `EncoderTargetBitrate bitrate.bitrate_kbps` | 同左，使用 struct | 保持设计一致 |
| `RtpExtension::kMinId` 为 `RtpHeaderExtensionId` | `constexpr int kMinId = 1` | C++17 不允许同一类型的 `static constexpr` 成员 |
