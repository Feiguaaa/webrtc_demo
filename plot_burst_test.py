#!/usr/bin/env python3
"""Generate charts from WebRTC receiver CSV.

Chart 1: Encoder target bitrate over time.
Chart 2: Per-frame received packets + lost packets + lost frames.
"""

import sys
import csv
import matplotlib.pyplot as plt
import numpy as np


def detect_format(csv_path):
    """Detect whether this is an rtp_session (RTP layer) or session (renderer layer) CSV."""
    with open(csv_path, "r") as f:
        first_line = f.readline().strip()
    return "rtp_session" if "frame_seq" in first_line else "session"


def plot_rtp_session(csv_path):
    """Plot from rtp_session CSV (RTP layer — precise per-frame loss)."""
    frame_numbers = []
    total_packets = []
    received_packets = []
    lost_packets = []
    loss_rates = []
    timestamps = []
    target_bitrates = []

    with open(csv_path, "r") as f:
        lines = [l for l in f if not l.startswith("#") and l.strip()]
        reader = csv.DictReader(lines)
        for row in reader:
            frame_numbers.append(int(row["frame_number"]))
            total_packets.append(int(row["total_packets"]))
            received_packets.append(int(row["received_packets"]))
            lost_packets.append(int(row["lost_packets"]))
            loss_rates.append(float(row["loss_rate"]))
            timestamps.append(int(row["timestamp_us"]))
            target_bitrates.append(int(row.get("target_bitrate_kbps", 0)))

    if not frame_numbers:
        print(f"No data in {csv_path}")
        return

    total_frames = len(frame_numbers)
    total_recv = sum(received_packets)
    total_lost = sum(lost_packets)
    total_exp = sum(total_packets)
    overall_loss = total_lost / total_exp * 100 if total_exp > 0 else 0

    # Detect burst frames (loss_rate > 0).
    burst_frames = [i for i, lr in enumerate(loss_rates) if lr > 0]

    # Keyframe detection (frames with >= 15 packets are likely keyframes).
    kf_idx = [i for i, tp in enumerate(total_packets) if tp >= 15]
    kf_val = [total_packets[i] for i in kf_idx]

    # Use real encoder target bitrate from CSV (EncoderTargetBitrateExtension).
    # Forward-fill zeros (missing data from lost first packets).
    filled_bitrates = []
    last_valid = 0
    for br in target_bitrates:
        if br > 0:
            last_valid = br
        filled_bitrates.append(last_valid)
    target_bitrates = filled_bitrates

    nonzero_br = [b for b in target_bitrates if b > 0]
    avg_br = int(np.median(nonzero_br)) if nonzero_br else 0
    max_br = max(nonzero_br) if nonzero_br else 0

    print(f"Total frames: {total_frames}")
    print(f"Total packets: sent={total_exp} received={total_recv} lost={total_lost} ({overall_loss:.1f}%)")
    print(f"Frames with loss: {len(burst_frames)}")
    print(f"Encoder target bitrate: median={avg_br} kbps, max={max_br} kbps")

    fig, axes = plt.subplots(2, 1, figsize=(16, 10), sharex=True)
    x = np.arange(len(frame_numbers))

    # ===== Chart 1: Per-frame packets (stacked) =====
    ax1 = axes[0]
    ax1.bar(x, lost_packets, color="#F44336", width=0.8, alpha=0.8,
            label="Lost packets")
    ax1.bar(x, received_packets, bottom=lost_packets, color="#2196F3",
            width=0.8, alpha=0.7, label="Received packets")
    if kf_idx:
        ax1.scatter(kf_idx, kf_val, color="darkred", s=30, zorder=5,
                    label="Keyframe")
    ax1.set_ylabel("Packets per Frame", fontsize=11)
    ax1.set_ylim(0, max(total_packets) * 1.3 if total_packets else 50)
    ax1.legend(loc="upper right", fontsize=9)
    ax1.grid(True, alpha=0.3)
    ax1.set_title(
        f"Per-Frame Packets (RTP Layer)  |  {total_frames} frames  |  "
        f"loss={total_lost}/{total_exp} ({overall_loss:.1f}%)",
        fontsize=12
    )

    # ===== Chart 2: Encoder target bitrate (from RTP header extension) =====
    ax2 = axes[1]
    ax2.plot(x, target_bitrates, "b-", linewidth=1.0, alpha=0.8,
             label="Encoder target bitrate")
    if avg_br > 0:
        ax2.axhline(y=avg_br, color="orange", linestyle="--", alpha=0.6,
                    label=f"Median: {avg_br} kbps")

    # Mark burst frames with vertical lines.
    if burst_frames:
        for i in burst_frames[:5]:  # label first few
            ax2.axvline(x=i, color="red", linestyle="-", alpha=0.15, linewidth=0.5)
        ax2.axvspan(burst_frames[0], burst_frames[-1],
                    color="red", alpha=0.05, label="Burst loss period")

    ax2.set_ylabel("Bitrate (kbps)", fontsize=11)
    ax2.set_xlabel("Frame Number", fontsize=11)
    br_ylim = max(max_br * 1.2, 500)
    ax2.set_ylim(0, min(br_ylim, 10000))
    ax2.legend(loc="upper right", fontsize=9)
    ax2.grid(True, alpha=0.3)
    ax2.set_title(
        f"Encoder Target Bitrate (from RTP header)  |  median={avg_br} kbps, max={max_br} kbps",
        fontsize=12
    )

    plt.tight_layout()
    output_path = "./rtp_burst_test.png"
    plt.savefig(output_path, dpi=150, bbox_inches="tight")
    print(f"\nSaved chart to {output_path}")
    plt.close()


def main():
    csv_path = sys.argv[1] if len(sys.argv) > 1 else "output/session_latest.csv"
    fmt = detect_format(csv_path)

    if fmt == "rtp_session":
        plot_rtp_session(csv_path)
        return

    # --- original session CSV plotting code below ---
    frame_numbers = []
    packets = []
    lost = []
    loss_rates = []
    cum_rates = []
    bitrates = []
    widths = []
    heights = []
    lost_frames = []
    total_lost_frames = 0
    total_sent = 0

    with open(csv_path, "r") as f:
        for line in f:
            line = line.strip()
            if line.startswith("# SUMMARY:"):
                # Parse summary: # SUMMARY: received=N lost_frames=M total_sent=T frame_loss_rate=R
                parts = line.split()
                for p in parts:
                    if p.startswith("lost_frames="):
                        total_lost_frames = int(p.split("=")[1])
                    elif p.startswith("total_sent="):
                        total_sent = int(p.split("=")[1])
                continue
            if line.startswith("#") or not line:
                continue
            # Parse CSV row.
            if not frame_numbers:
                # First data line - read header.
                reader = csv.DictReader([line] + [])
                # We need to handle this differently.
                break

    # Re-read properly with DictReader.
    frame_numbers = []
    packets = []
    lost = []
    loss_rates = []
    cum_rates = []
    bitrates = []
    widths = []
    heights = []
    lost_frames = []

    with open(csv_path, "r") as f:
        lines = [l for l in f if not l.startswith("#") and l.strip()]
        reader = csv.DictReader(lines)
        for row in reader:
            frame_numbers.append(int(row["frame_number"]))
            packets.append(int(row["received_packets"]))
            lost.append(int(row["lost_packets"]))
            loss_rates.append(float(row["loss_rate"]))
            cum_rates.append(float(row["cumulative_loss_rate"]))
            bitrates.append(int(row["bitrate_kbps"]))
            widths.append(int(row["width"]))
            heights.append(int(row["height"]))
            lost_frames.append(int(row.get("lost_frames", 0)))

    if not frame_numbers:
        print(f"No data in {csv_path}")
        return

    total_frames = len(frame_numbers)
    total_received = sum(packets)
    total_lost = sum(lost)
    total_expected = total_received + total_lost
    overall_loss = total_lost / total_expected * 100 if total_expected > 0 else 0
    nonzero_br = [b for b in bitrates if b > 0]
    avg_br = sum(nonzero_br) // len(nonzero_br) if nonzero_br else 0
    max_br = max(nonzero_br) if nonzero_br else 0
    total_lost_frm = sum(lost_frames)

    print(f"Total frames received: {total_frames}")
    print(f"Total completely lost frames: {total_lost_frm} (detected via frame_sequence gaps)")
    print(f"Total packets received: {total_received}")
    print(f"Total packets lost (within decoded frames): {total_lost}")
    print(f"Overall packet loss rate: {overall_loss:.1f}%")
    print(f"Bitrate: avg={avg_br} kbps, max={max_br} kbps")
    if total_sent > 0:
        print(f"Estimated sender frames: {total_sent}, frame loss = {total_lost_frames}/{total_sent} = {total_lost_frames/total_sent*100:.1f}%")

    # Detect resolution change points.
    res_changes = [i for i in range(1, len(widths)) if widths[i] != widths[i - 1]]

    fig, axes = plt.subplots(2, 1, figsize=(16, 10), sharex=False)

    # ===== Chart 1: Encoder target bitrate =====
    ax1 = axes[0]
    x = np.arange(len(frame_numbers))
    ax1.plot(x, bitrates, "b-", linewidth=1.0, alpha=0.8, label="Encoder target bitrate")

    # Mark resolution changes.
    for i in res_changes:
        ax1.axvline(x=i, color="gray", linestyle=":", alpha=0.4)

    ax1.axhline(y=avg_br, color="orange", linestyle="--", alpha=0.6,
                label=f"Average: {avg_br} kbps")
    ax1.set_ylabel("Bitrate (kbps)", fontsize=11)
    ax1.set_ylim(0, max_br * 1.3 if max_br > 0 else 4000)
    ax1.legend(loc="upper right", fontsize=9)
    ax1.grid(True, alpha=0.3)
    ax1.set_title(
        f"Encoder Target Bitrate Over Time  |  {total_frames} frames  |  "
        f"avg={avg_br} kbps, max={max_br} kbps",
        fontsize=12
    )

    # ===== Chart 2: Packets received + lost per frame =====
    ax2 = axes[1]

    # Stacked bar: lost (red from bottom) + received (blue on top).
    bar_width = 0.8
    ax2.bar(x, lost, color="#F44336", width=bar_width, alpha=0.8,
            label="Lost packets (within frame)")
    ax2.bar(x, packets, bottom=lost, color="#2196F3", width=bar_width,
            alpha=0.7, label="Received packets")

    # Mark completely lost frames (lost_frames > 0).
    lf_idx = [i for i, lf in enumerate(lost_frames) if lf > 0]
    if lf_idx:
        for i in lf_idx:
            ax2.axvline(x=i, color="red", linestyle="-", alpha=0.3, linewidth=0.8)
        # Label the first lost-frame marker.
        first_lf = lf_idx[0]
        ax2.annotate(f"{len(lf_idx)} frames lost",
                     xy=(first_lf, ax2.get_ylim()[1] * 0.5),
                     color="red", fontsize=8, rotation=90,
                     verticalalignment="center")

    # Mark keyframes (frames with >= 15 packets are likely keyframes).
    kf_idx = [i for i, p in enumerate(packets) if p >= 15]
    kf_val = [packets[i] for i in kf_idx]
    if kf_idx:
        ax2.scatter(kf_idx, kf_val, color="darkred", s=30, zorder=5,
                    label="Keyframe")

    # Resolution change markers.
    for i in res_changes:
        ax2.axvline(x=i, color="gray", linestyle=":", alpha=0.4)

    ax2.set_ylabel("Packets per Frame", fontsize=11)
    ax2.set_xlabel("Frame Number", fontsize=11)
    ax2.legend(loc="upper right", fontsize=9)
    ax2.grid(True, alpha=0.3, axis="y")
    ax2.set_title(
        f"Per-Frame Packets  |  pkt loss={total_lost} ({overall_loss:.1f}%)  |  "
        f"completely lost frames={total_lost_frm}",
        fontsize=12
    )

    plt.tight_layout()
    output_path = "./webrtc_burst_test.png"
    plt.savefig(output_path, dpi=150, bbox_inches="tight")
    print(f"\nSaved chart to {output_path}")
    plt.close()


if __name__ == "__main__":
    main()
