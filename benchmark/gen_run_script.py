#!/usr/bin/env python3
import argparse
import datetime
import itertools
import json
import os
import shutil
import sys

spec_commands = "benchmark/spec-commands.json"
spec_selected = ["519.lbm_r", "538.imagick_r", "505.mcf_r", "557.xz_r"]


def generate_configurations():
    crypto_params = {
        "crypto-aes-enc-cycles": [80, 80, 80, 80, 80, 80, 0],
        "crypto-aes-dec-cycles": [80, 80, 80, 80, 80, 80, 0],
        "crypto-aes-enc-ii": [20, 20, 20, 20, 20, 20, 0],
        "crypto-aes-dec-ii": [20, 20, 20, 20, 20, 20, 0],
        "crypto-mac-cycles": [40, 40, 40, 40, 40, 40, 0],
        "crypto-mac-ii": [10, 10, 10, 10, 10, 10, 0],
        "metadata-cache-size": [
            "4KiB",
            "4KiB",
            "4KiB",
            "4096KiB",
            "4KiB",
            "4KiB",
            "4KiB",
        ],
        "metadata-cache-assoc": [8, 8, 16, 8, 8, 1, 8],
        "int-trb-size": [32, 1024, 32, 32, 64, 32, 32],
    }

    configurations = [
        dict() for _ in range(len(crypto_params[list(crypto_params)[0]]))
    ]
    for param in crypto_params:
        for i, value in enumerate(crypto_params[param]):
            configurations[i][param] = value

    yield from configurations


def print_configurations():
    print(f"configuration information:")
    for i, conf in enumerate(generate_configurations()):
        print(f"  memsec_{i} {conf}")


def main(args):
    if args.list_configurations:
        print_configurations()
        return

    print("note: this script is meant to be run in the gem5 root folder")

    cpu = "o3"
    with open(spec_commands) as spec_commands_file:
        commands = json.loads(spec_commands_file.read())

    cmds = []
    for bench in commands["benchmarks"]:
        if bench not in spec_selected:
            continue
        cmd = os.path.join(
            commands["specdir"], commands["benchmarks"][bench]["command"]
        )
        runs = commands["benchmarks"][bench]["runs"]
        stdins = commands["benchmarks"][bench]["stdins"]
        stdouts = commands["benchmarks"][bench]["stdouts"]
        stderrs = commands["benchmarks"][bench]["stderrs"]
        for run_id, (run, stdin, stdout, stderr) in enumerate(
            zip(runs, stdins, stdouts, stderrs)
        ):
            if args.dry:
                cmd_options = f" {run}" if run else ""
                cmd_stdin = f" < {stdin}" if stdin else ""
                cmd_stdout = f" > {stdout}" if stdout else ""
                cmd_stderr = f" 2> {stderr}" if stderr else ""
                print(f"{cmd}{run}{cmd_stdin}{cmd_stdout}{cmd_stderr}")
                continue
            misc = [
                f.split("/")[-1]
                for f in run.split(" ")
                if "." in f and not (f.endswith("out") or f.endswith("err"))
            ]
            common = os.path.join(commands["specdir"], bench, "data")
            paths = [
                os.path.join(common, "refrate", "input"),
                os.path.join(common, "refrate", "output"),
                os.path.join(common, "all", "input"),
                os.path.join(common, "all", "output"),
            ]
            for f in misc:
                # skip import of existing files
                if os.path.exists(
                    os.path.join(os.path.join("benchmark", bench), f)
                ):
                    continue
                # import necessary resources from spec folder
                for p in paths:
                    joined = os.path.join(p, f)
                    if os.path.exists(joined):
                        print(f"copying from {joined} to {bench}")
                        shutil.copy(joined, os.path.join("benchmark", bench))
                        break
            cmd_options = f" SPECOPTIONS=\"--options '{run}'\"" if run else ""
            cmd_stdin = f' SPECSTDIN="--input {stdin}"' if stdin else ""
            cmd_stdout = f' SPECSTDOUT="--output {stdout}"' if stdout else ""
            cmd_stderr = f' SPECSTDERR="--errout {stderr}"' if stderr else ""
            cmds.append(
                (
                    cmd,
                    bench,
                    run_id,
                    f"{cmd_options}{cmd_stdin}{cmd_stdout}{cmd_stderr}",
                )
            )

    if args.dry:
        return

    print_configurations()

    # for now just print make commands instead of actually calling make
    with open(args.out_path, "w") as out_file:
        out_file.write("#!/bin/sh -x\n")
        for cmd, bench, run_id, extras in cmds:
            suffix = datetime.datetime.now().strftime("%m%d_%H%M")
            for i, conf in enumerate(generate_configurations()):
                # shorthand = "".join(["".join([w[0] for w in k.split("-")]) + str(conf[k]) for k in conf])
                outdir = os.path.join(
                    "result",
                    # f"{bench}_{run_id}_{suffix}_{shorthand}",
                    f"{bench}_{run_id}_{suffix}_memsec_{i}",
                )
                crypto_params = " ".join(
                    [
                        f"--{k}={f'\'{conf[k]}\'' if type(conf[k]) == str else conf[k]}"
                        for k in conf
                    ]
                )
                final_cmd = f'mkdir -p {outdir}; time make spec SPECOUTDIR={outdir} SPECCMD={cmd}{extras} MEMSEC=memsec GEM5_CONFIG_CRYPTO_PERF="{crypto_params}" 2>&1 > {outdir}/log &'
                out_file.write(f"{final_cmd}\n")
            outdir = os.path.join(
                "result",
                f"{bench}_{run_id}_{suffix}_no_memsec",
            )
            final_cmd = f"mkdir -p {outdir}; time make spec SPECOUTDIR={outdir} SPECCMD={cmd}{extras} MEMSEC=no-memsec 2>&1 > {outdir}/log &"
            out_file.write(f"{final_cmd}\n")
        mode = os.stat(args.out_path).st_mode
        mode |= (mode & 0o444) >> 2
        os.chmod(args.out_path, mode)
        print(f"file {args.out_path} written")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "-o",
        "--out-path",
        default="run-spec-benchmarks.sh",
        help="Output file path to write resulting shell script to",
    )
    parser.add_argument(
        "-l",
        "--list-configurations",
        action=argparse.BooleanOptionalAction,
        help="List configurations only",
    )
    parser.add_argument(
        "-d",
        "--dry",
        action=argparse.BooleanOptionalAction,
        help="List commands to be run without running them",
    )
    args = parser.parse_args()
    main(args)
