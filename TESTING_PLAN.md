# WebRTC 5G 网络丢包测试方案

## 测试架构

```
发送端（有线网络） → 云服务器 120.25.195.49:8080（信令） → 接收端 Mac（5G 热点）
```

- **信令路径**: 发送端 ↔ 云服务器 ↔ 接收端（HTTP 长连接，轻量）
- **媒体路径**: 发送端 → 接收端（WebRTC ICE UDP 直连，走公网，数据量大）
- **丢包位置**: 云服务器 → 接收端（5G 链路），自然丢包，无需模拟

## 测试流程

1. **启动发送端**（有线网络，稳定）：
   ```bash
   ./run_sender_loop.sh --server=120.25.195.49 --port=8080 --video_dir=/path/to/videos/
   ```

2. **启动接收端**（5G 热点）：
   ```bash
   ./out/webrtc_receiver --server=120.25.195.49 --port=8080 --low_latency --reconnect
   ```

3. **长时间运行**（24-48 小时），接收端断连自动重连

4. **分析结果**：批量处理所有 CSV 文件

## 数据记录

### CSV 分文件策略

每次接收端运行生成独立文件：
```
output/
  session_20260617_143000.csv
  session_20260617_180000.csv
  ...
```

### CSV 字段

| 列名 | 含义 |
|------|------|
| `frame_number` | 帧序号 |
| `width` | 帧宽 |
| `height` | 帧高 |
| `received_packets` | 该帧收到的 RTP 包数 |
| `skipped_frames` | 因该帧不完整跳过的帧数 |
| `loss_rate` | 瞬时丢包率 |
| `cumulative_loss_rate` | 累积丢包率 |
| `bitrate_kbps` | 码率 (kbps) |

## 分析脚本

- `plot_loss.py` — 瞬时 + 累积丢包率
- `plot_packets.py` — 每帧包数 + 跳帧事件
- `plot_intra_loss.py` — 帧内包级丢包

## 辅助脚本

- `run_sender_loop.sh` — 循环播放目录下所有 .y4m 视频
- `run_receiver_loop.sh` — 接收端断连自动重连（内建 `--reconnect` flag）

## 运行示例

```bash
# 发送端（循环播放视频）
./run_sender_loop.sh --server=120.25.195.49 --port=8080 --video_dir=/path/to/videos/

# 接收端（自动重连）
./out/webrtc_receiver --server=120.25.195.49 --port=8080 --low_latency --reconnect
```

## 关键指标

1. **分辨率稳定性** — 是否频繁降分辨率
2. **码率波动** — 突发丢包后的恢复时间
3. **帧跳过** — 5G 网络下跳帧的频率和严重程度
4. **重连行为** — 5G 断连后 ICE 重连速度
