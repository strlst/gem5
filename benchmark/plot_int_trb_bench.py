#!/usr/bin/env python3
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import seaborn as sns


def read_data(filename):
    with open(filename) as f:
        header = f.readline()
        body = f.read().strip()
        data = [line.split(";") for line in body.split("\n")]
        int_trb_size = np.array([int(row[0]) for row in data])
        latency = np.array([int(row[1]) for row in data])
    return int_trb_size, latency


def plot_data(int_trb_size, latency):
    df = pd.DataFrame(
        {
            "int_trb_size": int_trb_size,
            "latency": latency,
        }
    )

    # seaborn style and palette
    sns.set(
        style="whitegrid",
        context="notebook",
        rc={"axes.titlesize": 14, "axes.labelsize": 12},
    )
    palette = sns.color_palette("muted")

    # create figure
    plt.figure(figsize=(9, 5), dpi=200)
    plt.rcParams.update(
        {
            "text.usetex": True,
            "font.family": "sans-serif",
            "font.size": "26",
        }
    )

    # Line plot with markers
    ax = sns.lineplot(
        data=df,
        x="int_trb_size",
        y="latency",
        marker="o",
        linewidth=2.2,
        markersize=8,
        color=palette[0],
    )

    # Set x-axis to log scale
    ax.set_xscale("log")
    ax.set_xticks(df["int_trb_size"])
    ax.get_xaxis().set_major_formatter(
        plt.FuncFormatter(lambda val, pos: f"{int(val)}")
    )

    # Format y-axis with thousands separators
    ax.set_ylabel("latency [ticks]")
    ax.set_xlabel("int_trb_size (log scale)")
    ax.set_title("Latency vs IntTRB size")

    # Annotate each point with a compact label
    for x, y in zip(df["int_trb_size"], df["latency"]):
        ax.annotate(
            f"{y:,}",
            xy=(x, y),
            xytext=(0, 6),
            textcoords="offset points",
            ha="center",
            fontsize=9,
            color="black",
        )

    # Tight layout, legend, and save
    sns.despine(trim=True)
    plt.tight_layout()
    plt.savefig(
        "plots/latency_vs_int_trb_size.png", dpi=200, bbox_inches="tight"
    )
    plt.close()

    print("saved: plots/latency_vs_int_trb_size.png")


def main():
    # simple data processing
    fields = read_data("result/int-trb-sizes-latencies")
    plot_data(*fields)


if __name__ == "__main__":
    main()
