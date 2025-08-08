#!/usr/bin/env python3
import argparse
import datetime
import json
import os
import shutil
import sys

spec_commands = "benchmark/spec-commands.json"


def main(args):
    cpu = "o3"
    with open(spec_commands) as spec_commands_file:
        commands = json.loads(spec_commands_file.read())

    cmds = []
    for bench in commands["benchmarks"]:
        cmd = os.path.join(
            commands["specdir"], commands["benchmarks"][bench]["command"]
        )
        runs = commands["benchmarks"][bench]["runs"]
        for run_id, run in enumerate(runs):
            if args.dry:
                print(f"{cmd} {run}")
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
            cmds.append((cmd, run, bench, run_id))

    if args.dry:
        return

    # for now just print make commands instead of actually calling make
    for cmd, options, bench, run_id in cmds:
        suffix = datetime.datetime.now().strftime("%m%d%M%S")
        outdir = os.path.join("result", f"{bench}_{run_id}_{suffix}")
        os.makedirs(outdir, exist_ok=True)
        final = f'time make spec SPECOUTDIR={outdir} SPECCMD={cmd} SPECOPTIONS="{options}" 2>&1 | tee {outdir}/log &'
        print(final)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "-d",
        "--dry",
        action=argparse.BooleanOptionalAction,
        help="List commands to be run without running them",
    )
    args = parser.parse_args()
    main(args)
