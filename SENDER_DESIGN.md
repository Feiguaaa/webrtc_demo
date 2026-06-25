# Sender 端 RTP Header 扩展设计规范

## 整体目标

Sender 在每个视频 RTP 包中携带两个自定义扩展，receiver 端的 `GetExtension<FramePacketInfoExtension>` 和 `GetExtension<EncoderTargetBitrateExtension>` 才能解析成功。

---

## 1. SDP 协商（关键第一步）

在 SDP Offer/Answer 的 `m=video` 段中，必须注册这两个扩展的 **URI 和 ID**：

```
a=extmap:5 http://www.webrtc.org/experiments/rtp-hdrext/frame_packet_info
a=extmap:4 http://www.webrtc.org/experiments/rtp-hdrext/encoder_target_bitrate
```

| 扩展 ID | URI | 数据长度 |
|---------|-----|----------|
| 5 | `http://www.webrtc.org/experiments/rtp-hdrext/frame_packet_info` | 4 bytes |
| 4 | `http://www.webrtc.org/experiments/rtp-hdrext/encoder_target_bitrate` | 2 bytes |

这两个 URI 必须在 WebRTC 核心代码中注册。对应修改在 `media/engine/webrtc_video_engine.cc` 的 `GetRtpHeaderExtensions()` 函数：

```cpp
// 在视频扩展列表中加入：
result.emplace_back(RtpExtension::kFramePacketInfoUri, 5, RtpTransceiverDirection::kSendRecv);
result.emplace_back(RtpExtension::kEncoderTargetBitrateUri, 4, RtpTransceiverDirection::kSendRecv);
```

对应的 URI 常量定义在 `modules/rtp_rtcp/source/rtp_header_extensions.h`：

```cpp
constexpr char kFramePacketInfoUri[] =
    "http://www.webrtc.org/experiments/rtp-hdrext/frame_packet_info";
constexpr char kEncoderTargetBitrateUri[] =
    "http://www.webrtc.org/experiments/rtp-hdrext/encoder_target_bitrate";
```

---

## 2. RTP 包结构（线路格式）

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|V=2|P|X|  CC   |M|     PT      |       Sequence Number         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                           Timestamp                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                           SSRC                                |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| CSRCs (optional, 4 bytes each)...                             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| 0xBE | 0xDE  |   Header Extension Count (2 bytes, big-endian) |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  ID=5 |LEN=3|  FramePacketInfo Byte 0  |  FramePacketInfo Byte 1 |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  FramePacketInfo Byte 2  |  FramePacketInfo Byte 3          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  ID=4 |LEN=1|  EncoderTargetBitrate B0   |  EncoderTargetBitrate B1 |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         Payload...                            |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

---

## 3. 扩展详细格式

### 3.1 扩展 ID=5：FramePacketInfoExtension（4 bytes）

RFC 5285 one-byte header 格式：

```
Byte 0: [ID=5 (4bit)] [LEN=3 (4bit)]     → 0x53
Byte 1-4: 数据（4 bytes）
```

4 字节数据结构（大端序）：

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  total_packets(10 bits)  | packet_index(6) | frame_sequence    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|    (cont.) frame_sequence(10 bits)  |       padding(6)         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| 字段 | 位宽 | 范围 | 说明 |
|------|------|------|------|
| `total_packets` | 10 bits | 0-1023 | 当前视频帧分了多少个 RTP 包 |
| `packet_index` | 6 bits | 0-63 | 当前包是该帧的第几个包（从 0 开始） |
| `frame_sequence` | 10 bits | 0-1023 | 帧序列号，每发完一帧 +1，到 1024 回绕 |
| `padding` | 6 bits | - | 补位到 4 字节，填 0 |

**字节布局（大端序）**：

```
Byte 0: total_packets[9:2]         (高 8 位)
Byte 1: (total_packets & 0x3F) << 2 | (packet_index >> 4)
Byte 2: frame_sequence[9:2]        (高 8 位)
Byte 3: (frame_sequence & 0x3F) << 2 | (padding & 0x3F)
```

更直观的位映射：

```
Byte 0:  T9 T8 T7 T6 T5 T4 T3 T2
Byte 1:  T1 T0 P5 P4 P3 P2 P1 P0    (T=total_packets, P=packet_index)
Byte 2:  F9 F8 F7 F6 F5 F4 F3 F2    (F=frame_sequence)
Byte 3:  F1 F0 _  _  _  _  _  _    (_ = padding, 填 0)
```

### 3.2 扩展 ID=4：EncoderTargetBitrateExtension（2 bytes）

```
Byte 0: [ID=4 (4bit)] [LEN=1 (4bit)]     → 0x41
Byte 1-2: 数据（2 bytes）
```

2 字节数据结构（大端序）：

```
 0                   1
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|    target_bitrate_kbps (16)   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| 字段 | 位宽 | 范围 | 说明 |
|------|------|------|------|
| `target_bitrate_kbps` | 16 bits | 0-65535 | GoogCC 目标码率，单位 kbps |

---

## 4. Sender 端发送逻辑伪代码

```cpp
// 状态变量（帧级别）
static uint16_t frame_sequence_counter = 0;  // 每帧递增，1024 回绕

// 假设一帧被编码器分成了 N 个 RTP 包
uint16_t total_packets = N;  // 必须 <= 1023

for (int i = 0; i < N; i++) {
    RtpPacket* packet = ...;  // 创建 RTP 包

    // --- 附加 FramePacketInfoExtension ---
    FramePacketInfo frame_info;
    frame_info.total_packets = total_packets;
    frame_info.packet_index = i;
    frame_info.frame_sequence = frame_sequence_counter;
    packet->SetExtension<FramePacketInfoExtension>(frame_info);

    // --- 附加 EncoderTargetBitrateExtension（仅首包）---
    // Receiver 端只在 packet_index == 0 时读取此扩展
    if (i == 0) {
        EncoderTargetBitrate bitrate;
        bitrate.bitrate_kbps = current_target_bitrate_kbps;  // 从 GoogCC 获取
        packet->SetExtension<EncoderTargetBitrateExtension>(bitrate);
    }

    // 发送
    transport_->SendPacket(packet);
}

// 整帧发送完毕后递增序列号
frame_sequence_counter++;
if (frame_sequence_counter >= 1024) {
    frame_sequence_counter = 0;  // 10-bit 回绕
}
```

---

## 5. 必须修改的文件清单（Sender 端）

| 文件 | 修改内容 |
|------|----------|
| `modules/rtp_rtcp/include/rtp_rtcp_defines.h` | 添加 `RtpExtension::kFramePacketInfoUri` 和 `kEncoderTargetBitrateUri` 外部声明 |
| `modules/rtp_rtcp/source/rtp_header_extensions.h` | 添加 `FramePacketInfoExtension` 和 `EncoderTargetBitrateExtension` 序列化/反序列化类 |
| `modules/rtp_rtcp/source/rtp_header_extension_map.cc` | 注册两个新扩展类型到 WebRTC 扩展注册表 |
| `api/rtp_parameters.h/cc` | 添加 `FramePacketInfo` 和 `EncoderTargetBitrate` 结构体定义 |
| `media/engine/webrtc_video_engine.cc` | 在 `GetRtpHeaderExtensions()` 中将两个 URI 加入 SDP 协商列表 |
| `modules/rtp_rtcp/source/rtp_sender_video.cc` | 在 `SendVideo()` 中调用 `SetExtension<FramePacketInfoExtension>` |

---

## 6. 验证方法

在 receiver 端运行后，前 3 个包会打印原始 RTP 头解析结果：

```
[RtpLossTracker] pkt#1: ssrc=xxx seq=xxx pt=103 size=xxx
[RtpLossTracker]   One-byte ext: 0xBEDE count=N
[RtpLossTracker]     ext ID=5 len=4 bytes=XXXXXXXX
[RtpLossTracker]   => FramePacketInfo: total=X idx=X frame_seq=X
[RtpLossTracker]     ext ID=4 len=2 bytes=XXXX
[RtpLossTracker]   => EncoderTargetBitrate: XXXX kbps
```

如果看到以下日志，说明对接成功，CSV 文件开始写入：

```
[RtpLossTracker] First extension parsed: seq=0 idx=0 total=5 (pkt 1/1)
```

CSV 输出路径：`output/rtp_session_YYYYMMDD_HHMMSS.csv`

列：`frame_seq, frame_number, total_packets, received_packets, lost_packets, loss_rate, timestamp_us, target_bitrate_kbps`
