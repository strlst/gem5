#!/usr/bin/env python3
import argparse
import os
import re
import sys

import matplotlib.pyplot as plt
import pandas as pd
import seaborn as sns

dramsim3_fields = {
    "num_cycles",
    "num_reads_done",
    "num_reads_done",
    "num_read_row_hits",
    "num_writes_done",
    "num_writes_done",
    "num_write_row_hits",
    "average_read_latency",
    "average_interarrival",
    "average_bandwidth",
}

gem5_fields = {
    "simSeconds",
    "simTicks",
    "simFreq",
    "simInsts",
    "hostInstRate",
    "system.cpu.numCycles",
    "system.cpu.ipc",
    "system.cpu.issueRate",
    "system.cpu.fuBusyRate",
    "system.cpu.branchPred.condPredicted",
    "system.cpu.branchPred.condPredictedTaken",
    "system.cpu.branchPred.condIncorrect",
    "system.cpu.dcache.overallMisses::total",
    "system.cpu.dcache.overallMissRate::total",
    "system.cpu.dcache.replacements",
    "system.cpu.icache.overallMisses::total",
    "system.cpu.icache.overallMissRate::total",
    "system.cpu.icache.replacements",
    "system.l2cache.overallMisses::total",
    "system.l2cache.overallMissRate::total",
    "system.l2cache.replacements",
    "system.metadata_cache.overallMisses::total",
    "system.metadata_cache.overallMissRate::total",
    "system.metadata_cache.replacements",
    "system.crypto_ctrl.successfulReads",
    "system.crypto_ctrl.successfulWrites",
    "system.crypto_ctrl.int_trb.enqueued",
    "system.mem_ctrl.bytesRead::cpu.inst",
    "system.mem_ctrl.bytesRead::cpu.data",
    "system.mem_ctrl.bytesRead::crypto_ctrl.int_trb",
    "system.mem_ctrl.bytesRead::total",
    "system.mem_ctrl.bytesWritten::total",
    "system.mem_ctrl.numReads::cpu.inst",
    "system.mem_ctrl.numReads::cpu.data",
    "system.mem_ctrl.numReads::crypto_ctrl.int_trb",
    "system.mem_ctrl.numReads::total",
    "system.mem_ctrl.numWrites::total",
    "system.mem_ctrl.bwRead::cpu.inst",
    "system.mem_ctrl.bwRead::cpu.data",
    "system.mem_ctrl.bwRead::crypto_ctrl.int_trb",
    "system.mem_ctrl.bwRead::total",
    "system.mem_ctrl.bwWrite::total",
    "system.mem_ctrl.bwTotal::cpu.inst",
    "system.mem_ctrl.bwTotal::cpu.data",
    "system.mem_ctrl.bwTotal::crypto_ctrl.int_trb",
    "system.mem_ctrl.bwTotal::total",
}


def check_fields(line, fields):
    for field in fields:
        if line.startswith(field):
            return True
            print(f"line {line} starts with {field}")
    return False


def save(args, statistics):
    if args.out_path:
        print(f'saving to file "{args.out_path}"')
        with open(args.out_path) as out:
            out.write(f"{str(statistics)}\n")


def dramsim3_split(line):
    field, unused, stat, comment = line.split(maxsplit=3)
    return field, stat, comment.lstrip("# ")


def gem5_split(line):
    field, stat, comment = line.split(maxsplit=2)
    return field, stat, comment.lstrip("# ")


def analyze(statistics, target, fname, fields, splitfunc):
    statistics[target] = dict()
    obj = statistics[target]
    channel = None
    with open(fname) as stats_file:
        for line in map(str.strip, stats_file):
            if len(line) == 0:
                continue
            elif result := re.match(r"## Statistics of Channel (\d+)", line):
                channel = result.groups()[0]
                obj = statistics[target][f"channel{channel}"] = dict()
                continue
            elif line.startswith("#"):
                continue
            elif not check_fields(line, fields):
                continue
            field, stat, comment = splitfunc(line)
            obj[field] = (stat, comment)
            print(
                f"{target}: extracted {stat} on field {field} with comment {comment}{f' on channel {channel}' if channel else ''}"
            )


def str_after_n_th_index(string, char, n):
    i = -1
    for _ in range(n):
        i = string.find(char, i + 1)
    return string[i + 1 :]


def str_before_n_th_index(string, char, n):
    i = -1
    for _ in range(n):
        i = string.find(char, i + 1)
    return string[:i]


def prepare_df(df):
    df = df.sort_index().reset_index().rename(columns={"index": "benchmark"})
    df["mode"] = df["benchmark"].map(lambda s: str_after_n_th_index(s, "_", 5))
    df["benchmark"] = df["benchmark"].map(
        lambda s: str_before_n_th_index(s, "_", 3)
    )
    return df


def plot_stat(df, stat, title, simulator):
    sns.set(style="whitegrid")
    fig, axes = plt.subplots(nrows=2, ncols=1, figsize=(24, 20), sharex=True)
    plt.rcParams.update(
        {
            "text.usetex": True,
            "font.family": "sans-serif",
            "font.size": "26",
        }
    )
    plt.xticks(rotation=90)
    for i, data in enumerate([df, df[df["mode"] != "no_memsec"]]):
        ax = sns.barplot(
            x="benchmark", y=stat, data=data, hue="mode", ax=axes[i]
        )
        ax.legend(loc="upper right", bbox_to_anchor=(1.0, 1.0))
        ax.set_title(title)
    plt.tight_layout()

    filename = os.path.join("plots", f"{simulator}-{stat}.png")
    fig.savefig(filename)
    print(f"saved figure {simulator}-{stat} to file {filename}")

    plt.close(fig)


def main(args):
    statistics = dict()
    for root, dirs, files in os.walk("result"):
        if "dramsim3.txt" not in files or "stats.txt" not in files:
            print(f"skipping analysis of {root}")
            continue
        # print(root, dirs, files)
        statistics[root] = dict()
        analyze(
            statistics[root],
            "dramsim3",
            os.path.join(root, "dramsim3.txt"),
            dramsim3_fields,
            dramsim3_split,
        )
        analyze(
            statistics[root],
            "gem5",
            os.path.join(root, "stats.txt"),
            gem5_fields,
            gem5_split,
        )

    if args.out_path:
        save(args, statistics)

    # process extracted statistics
    data = {"by-dramsim3": dict(), "by-gem5": dict()}
    descriptions = dict()
    for benchmark in statistics:
        experiment = benchmark.lstrip("result/")
        for channel in statistics[benchmark]["dramsim3"]:
            if channel not in data["by-dramsim3"]:
                data["by-dramsim3"][channel] = dict()
            for stat in statistics[benchmark]["dramsim3"][channel]:
                if stat not in data["by-dramsim3"][channel]:
                    data["by-dramsim3"][channel][stat] = dict()
                value = statistics[benchmark]["dramsim3"][channel][stat][0]
                data["by-dramsim3"][channel][stat][experiment] = (
                    float(value) if "." in value else int(value)
                )
                if stat not in descriptions:
                    descriptions[stat] = statistics[benchmark]["dramsim3"][
                        channel
                    ][stat][1]
        for stat in statistics[benchmark]["gem5"]:
            if stat not in data["by-gem5"]:
                data["by-gem5"][stat] = dict()
            value = statistics[benchmark]["gem5"][stat][0]
            data["by-gem5"][stat][experiment] = (
                float(value) if "." in value else int(value)
            )
            if stat not in descriptions:
                descriptions[stat] = statistics[benchmark]["gem5"][stat][1]

    # create output dir
    os.makedirs("plots", exist_ok=True)
    print(f'created folder "plots"')

    df_gem5 = prepare_df(pd.DataFrame(data["by-gem5"]))
    for stat in gem5_fields:
        plot_stat(df_gem5, stat, descriptions[stat], "gem5")
    df_dramsim3 = prepare_df(pd.DataFrame(data["by-dramsim3"]["channel0"]))
    for stat in dramsim3_fields:
        plot_stat(df_dramsim3, stat, descriptions[stat], "dramsim3")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "-o",
        "--out-path",
        help="Output file path to write resulting shell script to",
    )
    parser.add_argument(
        "-d",
        "--dry",
        action=argparse.BooleanOptionalAction,
        help="List commands to be run without running them",
    )
    args = parser.parse_args()
    main(args)
