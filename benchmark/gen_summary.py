#!/usr/bin/env python3
import argparse
import os
import re
import sys

import matplotlib.pyplot as plt
import pandas as pd
import seaborn as sns

aim_overhead_binmask = 0xFFFFE0000000

dramsim3_fields = {
    "num_cycles",
    "num_reads_done",
    "num_reads_done",
    "num_read_row_hits",
    "num_writes_done",
    "num_writes_done",
    "num_write_row_hits",
    "average_read_latency",
    # "average_interarrival",
    "average_bandwidth",
}

gem5_fields = {
    "simSeconds",
    # "simTicks",
    # "simFreq",
    "simInsts",
    "hostInstRate",
    "system.cpu.numCycles",
    "system.cpu.ipc",
    "system.cpu.issueRate",
    # "system.cpu.fuBusyRate",
    # "system.cpu.branchPred.condPredicted",
    # "system.cpu.branchPred.condPredictedTaken",
    # "system.cpu.branchPred.condIncorrect",
    # "system.cpu.dcache.overallMisses::total",
    "system.cpu.dcache.overallMissRate::total",
    # "system.cpu.dcache.overallAvgMissLatency::total",
    # "system.cpu.dcache.replacements",
    # "system.cpu.icache.overallMisses::total",
    "system.cpu.icache.overallMissRate::total",
    # "system.cpu.icache.overallAvgMissLatency::total",
    # "system.cpu.icache.replacements",
    # "system.l2cache.overallMisses::total",
    "system.l2cache.overallMissRate::total",
    "system.l2cache.overallAvgMissLatency::total",
    # "system.l2cache.replacements",
    "system.metadata_cache.overallHits::total",
    "system.metadata_cache.overallMisses::total",
    "system.metadata_cache.overallMissRate::total",
    # "system.metadata_cache.overallAvgMissLatency::total",
    "system.metadata_cache.replacements",
    "system.aim_ctrl.totalRequests",
    "system.aim_ctrl.successfulRequests",
    "system.aim_ctrl.refusedRequests",
    # "system.aim_ctrl.successfulReads",
    # "system.aim_ctrl.successfulWrites",
    "system.aim_ctrl.int_trb.enqueued",
    # "system.mem_ctrl.bytesRead::cpu.inst",
    # "system.mem_ctrl.bytesRead::cpu.data",
    # "system.mem_ctrl.bytesRead::aim_ctrl.int_trb",
    # "system.mem_ctrl.bytesRead::total",
    # "system.mem_ctrl.bytesWritten::total",
    # "system.mem_ctrl.numReads::cpu.inst",
    # "system.mem_ctrl.numReads::cpu.data",
    # "system.mem_ctrl.numReads::aim_ctrl.int_trb",
    "system.mem_ctrl.numReads::total",
    "system.mem_ctrl.numWrites::total",
    # "system.mem_ctrl.bwRead::cpu.inst",
    # "system.mem_ctrl.bwRead::cpu.data",
    # "system.mem_ctrl.bwRead::aim_ctrl.int_trb",
    # "system.mem_ctrl.bwRead::total",
    # "system.mem_ctrl.bwWrite::total",
    # "system.mem_ctrl.bwTotal::cpu.inst",
    # "system.mem_ctrl.bwTotal::cpu.data",
    # "system.mem_ctrl.bwTotal::aim_ctrl.int_trb",
    "system.mem_ctrl.bwTotal::total",
}

color_mapping = {
    "memsec_none": "#f94144",
    "memsec_basic": "#f3722c",
    "memsec_big_mdcache": "#f8961e",
    "memsec_buff": "#f9844a",
    "memsec_big_buff": "#f9c74f",
    "memsec_merge": "#90be6d",
    "memsec_defrag": "#43aa8b",
    "memsec_similar": "#4d908e",
    "memsec_no_delay": "#577590",
    "memsec_aes_units": "#277da1",
    "memsec_full": "#0466c8",
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
            # print(
            # f"{target}: extracted {stat} on field {field} with comment {comment}{f' on channel {channel}' if channel else ''}"
            # )
    print(f"analyzed {fname}")


def summarize_statistics(args, statistics):
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
    os.makedirs(args.plots_path, exist_ok=True)
    print(f'created folder "{args.plots_path}"')

    df_gem5 = prepare_df(pd.DataFrame(data["by-gem5"]))
    for stat in gem5_fields:
        plot_stat_bar(
            df_gem5, stat, descriptions[stat], "gem5", args.plots_path
        )
        # plot_stat_line(df_gem5, stat, descriptions[stat], "gem5", args.plots_path)
    df_dramsim3 = prepare_df(pd.DataFrame(data["by-dramsim3"]["channel0"]))
    for stat in dramsim3_fields:
        plot_stat_bar(
            df_dramsim3, stat, descriptions[stat], "dramsim3", args.plots_path
        )
        # plot_stat_line(df_dramsim3, stat, descriptions[stat], "dramsim3", args.plots_path)


def bin_schedule(hits, fname):
    with open(fname) as overhead_file:
        # skip CSV header
        overhead_file.readline()
        for line in map(str.strip, overhead_file):
            # NOTE: hex addresses are at column 3, currently hardcoded
            binned = hex(int(line.split(";")[3], 16) & aim_overhead_binmask)
            hits[binned] = hits.get(binned, 0) + 1


def analyze_accesses(args, accesses):
    # NOTE: imbalance(k, l) is defined as l / (k + l) over the access counts
    # of any two regions of memory
    # for instance, we can compare region
    #   region(k)=[0, 0x17ff]
    # against region
    #   region(l)=[0x1800, 0x1fff]
    # NOTE: overhead(k, l) is defined as (k + l) / k
    imbalances = dict()
    overheads = dict()
    for benchmark in sorted(accesses):
        # prepare dict for plotting later
        experiment = str_before_n_th_index(benchmark.lstrip("result/"), "_", 3)
        arch = str_after_n_th_index(benchmark, "_", 5)
        if experiment not in imbalances:
            imbalances[experiment] = dict()
        if experiment not in overheads:
            overheads[experiment] = dict()

        # get access counts and calculate imbalance
        hits = accesses[benchmark]
        keys = list(hits.keys())
        for i, k in enumerate(keys):
            for l in keys[i + 1 :]:
                imbalance = round(hits[l] * 100 / (hits[k] + hits[l]), 2)
                # print(
                # f"{benchmark}: imbalance(@{k}, @{l}) = {hits[l]} / ({hits[k]} + {hits[l]}) = {imbalance}%"
                # )
                overhead = round((hits[k] + hits[l]) * 100 / hits[k], 2)
                # print(
                # f"{benchmark}: overhead(@{k}, @{l}) = ({hits[k]} + {hits[l]}) / {hits[k]} = {overhead}%"
                # )
                imbalances[experiment][arch] = imbalance
                overheads[experiment][arch] = overhead

    # finally plot and save data
    plot_accesses(imbalances, "Imbalance", args.plots_path)
    plot_accesses(overheads, "Overhead", args.plots_path)


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
    df["configuration"] = df["benchmark"].map(
        lambda s: str_after_n_th_index(s, "_", 5)
    )
    df["benchmark"] = df["benchmark"].map(
        lambda s: str_before_n_th_index(s, "_", 3)
    )
    return df


def plot_accesses(data, title, plots_path):
    sns.set(style="whitegrid")
    plt.rcParams.update(
        {
            "text.usetex": True,
            "font.family": "sans-serif",
            "font.size": "26",
        }
    )

    df = (
        pd.DataFrame.from_dict(data, orient="index")
        .reset_index()
        .rename(columns={"index": "benchmark"})
        .melt(
            id_vars="benchmark", var_name="configuration", value_name="value"
        )
    )
    df["benchmark"] = pd.Categorical(
        df["benchmark"], categories=list(data.keys()), ordered=True
    )

    plt.figure(figsize=(12, 8))
    ax = sns.lineplot(
        data=df,
        x="benchmark",
        y="value",
        hue="configuration",
        marker="o",
        estimator=None,
        sort=False,
    )
    for line in ax.lines:
        line.set_linestyle("--")
        line.set_linewidth(1.5)
    # plt.ylim(0, 100)
    plt.xticks(rotation=30, ha="right")
    plt.xlabel("Benchmark")
    plt.ylabel(f"Integrity Tree Memory Traffic {title} [\\%]")
    plt.title(f"{title} of different configurations across benchmarks")
    plt.legend(title="Configuration", loc="upper left")
    plt.tight_layout()

    lower_title = title.lower()
    filename = os.path.join(plots_path, f"dramsim3-schedule-{lower_title}.png")
    plt.savefig(filename, dpi=300, bbox_inches="tight")
    print(f"saved imbalance figure to file {filename}")

    plt.close()


def plot_stat_line(df, stat, title, simulator, plots_path):
    sns.set(style="whitegrid")
    plt.rcParams.update(
        {
            "text.usetex": True,
            "font.family": "sans-serif",
            "font.size": "26",
        }
    )

    # order x-axis by mean of stat (introduces non-consistent)
    # benchmark order!
    agg = df.groupby("benchmark")[stat].mean()
    ordered = agg.sort_values().index.tolist()
    df = pd.concat(
        [df[df["benchmark"] == b] for b in ordered], ignore_index=True
    )

    plt.figure(figsize=(12, 8))
    ax = sns.lineplot(
        data=df,
        x="benchmark",
        y=stat,
        hue="configuration",
        marker="o",
        estimator=None,
        sort=False,
    )
    for line in ax.lines:
        line.set_linestyle("--")
        line.set_linewidth(1.5)
    plt.xticks(rotation=30, ha="right")
    plt.xlabel("Benchmark")
    plt.ylabel(stat)
    # plt.title(f'{stat} per benchmark for each configuration')
    plt.title(title)
    plt.legend(title="Configuration", loc="upper left")
    plt.tight_layout()

    filename = os.path.join(plots_path, f"{simulator}-{stat}.png")
    plt.savefig(filename, dpi=300, bbox_inches="tight")
    print(f"saved figure {simulator}-{stat} to file {filename}")

    plt.close()


def plot_stat_bar(df, stat, title, simulator, plots_path):
    sns.set(style="whitegrid")
    plt.rcParams.update(
        {
            "text.usetex": True,
            "font.family": "sans-serif",
            "font.size": "26",
        }
    )
    plt.figure(figsize=(12, 8))
    ax = sns.barplot(
        x="benchmark",
        y=stat,
        data=df,
        hue="configuration",
        palette=color_mapping,
        hue_order=color_mapping.keys(),
    )
    plt.legend(title="Configuration", loc="upper left")
    ax.set_title(title)
    plt.xticks(rotation=30, ha="right")
    plt.tight_layout()

    filename = os.path.join(plots_path, f"{simulator}-{stat}.png")
    plt.savefig(filename, dpi=300, bbox_inches="tight")
    print(f"saved figure {simulator}-{stat} to file {filename}")

    plt.close()


def list_archs():
    archs = set()
    for root, dirs, files in os.walk("result"):
        if (
            "dramsim3.txt" not in files
            or "stats.txt" not in files
            or "dramsim3.schedule.txt" not in files
        ):
            continue
        archs.add(str_after_n_th_index(root, "_", 5))
    for arch in archs:
        print(arch)


def main(args):
    statistics = dict()
    accesses = dict()

    if args.list_architectures:
        list_archs()
        return

    if args.filter:
        whitelist = args.filter.split(",")
        # remove color mappings that are not contained in the whitelist
        unused = [key for key in color_mapping.keys() if key not in whitelist]
        for key in unused:
            del color_mapping[key]
        # print(whitelist)

    for root, dirs, files in os.walk("result"):
        # only include paths with results
        if (
            "dramsim3.txt" not in files
            or "stats.txt" not in files
            or "dramsim3.schedule.txt" not in files
        ):
            # print(f"skipping analysis of {root}")
            continue

        # skip architectures not included in whitelist
        if args.filter:
            arch = str_after_n_th_index(root, "_", 5)
            if arch not in whitelist:
                print(f"skipping non-whitelisted architecture {arch}")
                continue

        # print(root, dirs, files)
        statistics[root] = dict()
        accesses[root] = dict()

        # dramsim3 analysis branch
        if not args.skip_statistics and "dramsim3.txt" in files:
            analyze(
                statistics[root],
                "dramsim3",
                os.path.join(root, "dramsim3.txt"),
                dramsim3_fields,
                dramsim3_split,
            )

        # gem5 analysis branch
        if not args.skip_statistics and "stats.txt" in files:
            analyze(
                statistics[root],
                "gem5",
                os.path.join(root, "stats.txt"),
                gem5_fields,
                gem5_split,
            )

        # extra schedule/overhead analysis
        if not args.skip_imbalance and "dramsim3.schedule.txt" in files:
            bin_schedule(
                accesses[root],
                os.path.join(root, "dramsim3.schedule.txt"),
            )

    if args.out_path:
        save(args, statistics)

    if not args.skip_statistics:
        summarize_statistics(args, statistics)

    if not args.skip_imbalance:
        analyze_accesses(args, accesses)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "-o",
        "--out-path",
        help="Output file path to write resulting shell script to",
    )
    parser.add_argument(
        "-p",
        "--plots-path",
        default="plots",
        help="Path to folder where plots should be saved to",
    )
    parser.add_argument(
        "-s",
        "--skip-statistics",
        action=argparse.BooleanOptionalAction,
        help="Only summarize statistics from dramsim3 and gem5",
    )
    parser.add_argument(
        "-i",
        "--skip-imbalance",
        action=argparse.BooleanOptionalAction,
        help="Run only imbalance analysis on dramsim3 schedule files",
    )
    parser.add_argument(
        "-l",
        "--list-architectures",
        action=argparse.BooleanOptionalAction,
        help="List architectures that are analyzed and plotted",
    )
    parser.add_argument(
        "-f",
        "--filter",
        help="Filter architectures by name (comma-separated, e.g. no_memsec,memsec_basic)",
    )
    parser.add_argument(
        "-d",
        "--dry",
        action=argparse.BooleanOptionalAction,
        help="List commands to be run without running them",
    )
    args = parser.parse_args()
    main(args)
