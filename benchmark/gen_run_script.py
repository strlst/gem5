#!/usr/bin/env python3
import argparse
import datetime
import itertools
import json
import os
import shutil
import sys

spec_commands = "benchmark/spec-commands.json"
# spec_selected = ["519.lbm_r", "538.imagick_r", "505.mcf_r", "557.xz_r"]
spec_selected = [
    "503.bwaves_r",
    "507.cactuBSSN_r",
    "508.namd_r",
    "511.povray_r",
    "519.lbm_r",
    "538.imagick_r",
    # "544.nab_r",
    # "549.fotonik3d_r",
    # "554.roms_r",
    "997.specrand_fr",
    "502.gcc_r",
    # "505.mcf_r",
    # "520.omnetpp_r",
    "531.deepsjeng_r",
    "541.leela_r",
    "548.exchange2_r",
    "557.xz_r",
    # "999.specrand_ir",
]


def generate_configurations():
    # configuration k takes the k-th parameter of each list
    # each configuration might as well be hardcoded directly, but this way
    # saves spelling each parameter for each configuration
    aim_params = {
        "profile": [
            "basic",
            "buff",
            "merge",
            "defrag",
            "big_buff",
            "big_mdcache",
            "no_delay",
            "long_ii",
        ],
        "aim-aes-enc-cycles": [80, 80, 80, 80, 80, 80, 1, 80],
        "aim-aes-dec-cycles": [80, 80, 80, 80, 80, 80, 1, 80],
        "aim-aes-enc-ii": [10, 10, 10, 10, 10, 10, 1, 80],
        "aim-aes-dec-ii": [10, 10, 10, 10, 10, 10, 1, 80],
        "aim-mac-cycles": [40, 40, 40, 40, 40, 40, 1, 40],
        "aim-mac-ii": [5, 5, 5, 5, 5, 5, 1, 40],
        "metadata-cache-size": [
            "32KiB",
            "32KiB",
            "32KiB",
            "32KiB",
            "32KiB",
            "4096KiB",
            "32KiB",
            "32KiB",
        ],
        "metadata-cache-assoc": [8, 8, 8, 8, 8, 32, 8, 8],
        "int-trb-size": [1, 16, 16, 16, 128, 1, 1, 1],
        "int-merge-req": [2 <= i <= 4 for i in range(8)],
        "int-defrag-req": [3 <= i <= 4 for i in range(8)],
    }

    configurations = [
        dict() for _ in range(len(aim_params[list(aim_params)[0]]))
    ]
    for param in aim_params:
        for i, value in enumerate(aim_params[param]):
            configurations[i][param] = value

    yield from configurations


def print_configurations(table=False):
    if table:
        for i, conf in enumerate(generate_configurations()):
            if i == 0:
                print(";".join(key for key in conf))
            print(";".join(str(conf[key]) for key in conf))
    else:
        print(f"configuration information:")
        for i, conf in enumerate(generate_configurations()):
            print(f"  {conf["profile"]} {conf}")


def main(args):
    if args.list_configurations:
        print_configurations()
        return

    if args.show_configuration_table:
        print_configurations(table=True)
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
                    f"{bench}_{run_id}_{suffix}_memsec_{conf['profile']}",
                )
                # not the most elegant solution, but it works
                aim_params = " ".join(
                    [
                        (
                            (f"--{k}" if conf[k] else f"--no-{k}")
                            if type(conf[k]) == bool
                            else f"--{k}={f'\'{conf[k]}\'' if type(conf[k]) == str else conf[k]}"
                        )
                        for k in conf
                        if k != "profile"
                    ]
                )
                final_cmd = f'mkdir -p {outdir}; time make spec SPECOUTDIR={outdir} SPECCMD={cmd}{extras} MEMSEC=memsec GEM5_CONFIG_AIM_PERF="{aim_params}" 2>&1 > {outdir}/log &'
                out_file.write(f"{final_cmd}\n")
            outdir = os.path.join(
                "result",
                f"{bench}_{run_id}_{suffix}_no_memsec",
            )
            # final_cmd = f"mkdir -p {outdir}; time make spec SPECOUTDIR={outdir} SPECCMD={cmd}{extras} MEMSEC=no-memsec 2>&1 > {outdir}/log &"
            # only parallelize up to level of same bench
            final_cmd = f"mkdir -p {outdir}; time make spec SPECOUTDIR={outdir} SPECCMD={cmd}{extras} MEMSEC=no-memsec 2>&1 > {outdir}/log"
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
        "-t",
        "--show-configuration-table",
        action=argparse.BooleanOptionalAction,
        help="Show a table of all configurations only",
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
