#!/usr/bin/env python3
"""Plot per-frame packets with intra-frame loss rate overlay.

X-axis: frame number
Y1-axis (bars): packets received per frame (blue)
Y2-axis (line): intra-frame loss rate (red)

Intra-frame loss rate = (expected_packets - received_packets) / expected_packets
where expected_packets = median packet count of nearby frames at same resolution.

Run: python3 plot_intra_loss.py [csv_file]
"""

import sys
import csv
import matplotlib.pyplot as plt
import numpy as np


def estimate_expected_packets(frame_numbers, packets, widths, i):
    """Estimate expected packet count for frame i using nearby frames
    at the same resolution."""
    w = widths[i]
    p = packets[i]

    # Collect packet counts from nearby frames (window of 30) at same resolution
    # that are NOT loss events (skipped==0)
    # For simplicity, use all nearby frames at same resolution
    window = 30
    start = max(0, i - window)
    end = min(len(packets), i + window + 1)

    same_res_pkts = []
    for j in range(start, end):
        if widths[j] == w:
            same_res_pkts.append(packets[j])

    if not same_res_pkts:
        return p  # fallback

    median_pkt = int(np.median(same_res_pkts))

    # For received frames, expected >= received (can't receive more than expected)
    # But due to codec variation, received might occasionally exceed median.
    # Use max(median, received) as expected.
    return max(median_pkt, p)


def main():
    csv_path = sys.argv[1] if len(sys.argv) > 1 else "output/frame_loss.csv"

    frame_numbers = []
    packets = []
    skipped = []
    widths = []

    with open(csv_path, "r") as f:
        reader = csv.DictReader(f)
        for row in reader:
            frame_numbers.append(int(row["frame_number"]))
            packets.append(int(row["received_packets"]))
            skipped.append(int(row["skipped_frames"]))
            widths.append(int(row["width"]))

    if not frame_numbers:
        print(f"No data in {csv_path}")
        return

    # Calculate expected packets and intra-frame loss rate for each received frame.
    expected = []
    loss_rates = []
    for i in range(len(frame_numbers)):
        exp = estimate_expected_packets(frame_numbers, packets, widths, i)
        expected.append(exp)
        rate = (exp - packets[i]) / exp if exp > 0 else 0.0
        loss_rates.append(rate)

    total_expected = sum(expected)
    total_received = sum(packets)
    total_lost = total_expected - total_received

    print(f"Total received frames: {len(frame_numbers)}")
    print(f"Estimated total packets: {total_expected}")
    print(f"Actual total packets:    {total_received}")
    print(f"Estimated lost packets:  {total_lost}")
    print(f"Overall intra-frame loss: {total_lost/total_expected*100:.1f}%")

    # Print per-frame details for frames with non-zero loss.
    print("\nFrames with intra-frame packet loss:")
    for i, (f, p, e, r) in enumerate(zip(frame_numbers, packets, expected, loss_rates)):
        if r > 0.01:
            print(f"  Frame {f}: received={p}, expected={e}, loss_rate={r:.1%}")

    fig, ax1 = plt.subplots(figsize=(14, 5))

    x = np.arange(len(frame_numbers))
    bar_width = 0.35

    # Blue bars: packets received per frame (shifted left).
    ax1.bar(x - bar_width/2, packets, color="#2196F3", width=bar_width, alpha=0.7, label="Packets received")

    # Red bars: lost packets per frame, starting from y=0 (shifted right).
    lost_pkts = [e - p for e, p in zip(expected, packets)]
    ax1.bar(x + bar_width/2, lost_pkts, color="#F44336", width=bar_width, alpha=0.7, label="Lost packets")

    ax1.set_ylabel("Packets per Frame")
    ax1.set_xlabel("Frame Number")
    ax1.set_xlim(-5, len(frame_numbers) + 5)

    ax1.legend(loc="upper right")
    ax1.grid(True, alpha=0.3, axis="y")
    ax1.set_title(f"Intra-frame Packet Loss (total estimated loss: {total_lost} pkts, "
                  f"{total_lost/total_expected*100:.1f}%)")

    plt.tight_layout()
    output_path = "/tmp/intra_frame_loss.png"
    plt.savefig(output_path, dpi=150)
    print(f"\nSaved plot to {output_path}")
    plt.close()


if __name__ == "__main__":
    main()
