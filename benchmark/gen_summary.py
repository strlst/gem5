#!/usr/bin/env python3
import argparse
import os
import re
import sys

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
    "system.cpu.icache.overallMisses::total",
    "system.cpu.icache.overallMissRate::total",
    "system.l2cache.overallMisses::total",
    "system.l2cache.overallMissRate::total",
    "system.metadata_cache.overallMisses::total",
    "system.metadata_cache.overallMissRate::total",
    "system.crypto_ctrl.reads",
    "system.crypto_ctrl.writes",
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
    out = sys.stdout
    if args.out_path:
        print(f'saving to file "{args.out_path}"')
        out = open(args.out_path, "w")
    out.write(f"{str(statistics)}\n")
    out.close()


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
    save(args, statistics)


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
