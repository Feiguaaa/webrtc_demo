#!/usr/bin/env python3
"""Plot per-frame packet loss visualization.

Shows:
  1. Packets received per frame (colored by whether frames were skipped after it)
  2. Skipped frames caused by each frame
  3. Cumulative skipped frames

Run: python3 plot_packets.py [csv_file]
"""

import sys
import csv
import matplotlib.pyplot as plt
import numpy as np


def main():
    csv_path = sys.argv[1] if len(sys.argv) > 1 else "output/frame_loss.csv"

    frame_numbers = []
    packets = []
    skipped = []
    loss_rates = []
    cum_rates = []
    widths = []

    with open(csv_path, "r") as f:
        reader = csv.DictReader(f)
        for row in reader:
            frame_numbers.append(int(row["frame_number"]))
            packets.append(int(row["received_packets"]))
            skipped.append(int(row["skipped_frames"]))
            loss_rates.append(float(row["loss_rate"]))
            cum_rates.append(float(row["cumulative_loss_rate"]))
            widths.append(int(row["width"]))

    if not frame_numbers:
        print(f"No data in {csv_path}")
        return

    total_skipped = sum(skipped)
    total_received = len(frame_numbers)

    print(f"Total received frames: {total_received}")
    print(f"Total skipped frames:  {total_skipped}")
    print(f"Total expected frames: {total_received + total_skipped}")

    fig, axes = plt.subplots(3, 1, figsize=(14, 10), sharex=False)

    # Plot 1: Packets received per frame.
    # Color: blue = no frames skipped, red = some frames were skipped
    ax1 = axes[0]
    x = np.arange(len(frame_numbers))
    bar_width = 0.8
    colors = ["#F44336" if s > 0 else "#2196F3" for s in skipped]

    ax1.bar(x, packets, color=colors, width=bar_width, alpha=0.7)
    ax1.set_ylabel("Packets Received")
    ax1.grid(True, alpha=0.3, axis="y")

    # Legend.
    from matplotlib.patches import Patch
    legend_elements = [
        Patch(facecolor="#2196F3", alpha=0.7, label="No frames skipped"),
        Patch(facecolor="#F44336", alpha=0.7, label="Frames skipped (loss event)"),
    ]
    ax1.legend(handles=legend_elements, loc="upper right")

    # Mark keyframes.
    for i, p in enumerate(packets):
        if p >= 15:
            ax1.annotate("KF", xy=(i, p), xytext=(0, 2),
                        textcoords="offset points", ha="center",
                        fontsize=7, color="darkred", fontweight="bold")

    ax1.set_title(f"Packets Received per Frame (KF=keyframe, "
                  f"red=loss event caused {total_skipped} skipped frames total)")
    ax1.set_xticks([])

    # Plot 2: Skipped frames per frame (only non-zero).
    ax2 = axes[1]
    nonzero_idx = [i for i, s in enumerate(skipped) if s > 0]
    nonzero_val = [s for s in skipped if s > 0]
    ax2.bar(nonzero_idx, nonzero_val, color="#FF5722", width=bar_width, alpha=0.7)
    ax2.set_ylabel("Skipped Frames")
    ax2.set_xlabel("Frame Number")
    ax2.grid(True, alpha=0.3, axis="y")
    ax2.set_title(f"Skipped Frames per Loss Event (total={total_skipped})")

    # Annotate large events.
    for i, (idx, s) in enumerate(zip(nonzero_idx, nonzero_val)):
        if s >= 3:
            ax2.annotate(f"{s}", xy=(idx, s), xytext=(0, 3),
                        textcoords="offset points", ha="center", fontsize=8)

    # Plot 3: Cumulative skipped frames.
    ax3 = axes[2]
    cum_skipped = np.cumsum(skipped)
    ax3.plot(frame_numbers, cum_skipped, "r-", linewidth=1.5)
    ax3.fill_between(frame_numbers, cum_skipped, alpha=0.2, color="red")
    ax3.set_ylabel("Cumulative Skipped")
    ax3.set_xlabel("Frame Number")
    ax3.grid(True, alpha=0.3)
    ax3.set_title(f"Cumulative Skipped Frames (final={total_skipped} out of "
                  f"{total_received + total_skipped} expected = "
                  f"{total_skipped/(total_received+total_skipped)*100:.1f}%)")

    plt.tight_layout()
    output_path = "/tmp/packets_per_frame.png"
    plt.savefig(output_path, dpi=150)
    print(f"\nSaved plot to {output_path}")
    plt.close()


if __name__ == "__main__":
    main()
