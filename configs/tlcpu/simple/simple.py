import argparse
import sys

from caches import *
from memory import *

import m5
from m5.objects import *
from m5.util import (
    addToPath,
    fatal,
    warn,
)

addToPath("../../")

from common import (
    ObjectList,
    Options,
    Simulation,
)
from common.FileSystemConfig import config_filesystem


def add_common_args(parser):
    parser.add_argument(
        "-I",
        "--maxinsts",
        action="store",
        type=int,
        default=None,
        help="""Total number of instructions to
        simulate (default: run forever)""",
    )


def add_custom_args(parser):
    # parser.add_argument(
    # "binary",
    # default="tests/test-progs/matmul/bin/riscv/linux/matmul",
    # help="path to the binary to use as a bare-metal test",
    # )
    parser.add_argument(
        "--num-cores",
        default=1,
        type=int,
        nargs="?",
        help="number of CPU cores to instantiate",
    )
    parser.add_argument(
        "--l1i-size",
        default="16KiB",
        help="L1 instruction cache size (default: 16KiB)",
    )
    parser.add_argument(
        "--l1d-size",
        default="16KiB",
        help="L1 data cache size (default: 16KiB)",
    )
    parser.add_argument(
        "--l2-size", default="512KiB", help="L2 cache size (default: 256KiB)"
    )
    parser.add_argument(
        "--l3-size", default="4096KiB", help="L3 cache size (default: 4096KiB)"
    )
    parser.add_argument(
        "--l3",
        action=argparse.BooleanOptionalAction,
        help="flag to enable/disable existence of an L3 cache",
    )
    parser.add_argument(
        "--ooo",
        action=argparse.BooleanOptionalAction,
        help="flag to enable/disable use of O3 CPU model",
    )
    parser.add_argument(
        "--memsec",
        action=argparse.BooleanOptionalAction,
        help="flag to enable/disable memory security subsystem used for confidentiality and integrity",
    )
    parser.add_argument(
        "--crypto-aes-block-bits",
        default="128",
        type=int,
        help="AES block size in bits (default: 128)",
    )
    # default parameters sourced from
    # https://ieeexplore.ieee.org/document/7019004
    parser.add_argument(
        "--crypto-aes-enc-cycles",
        default="336",
        help="AES encryption delay in cycles (default: 336)",
    )
    parser.add_argument(
        "--crypto-aes-dec-cycles",
        default="216",
        type=int,
        help="AES decryption delay in cycles (default: 216)",
    )
    parser.add_argument(
        "--crypto-aes-enc-ii",
        default="336",
        type=int,
        help="AES encryption initiation interval in cycles (default: 336)",
    )
    parser.add_argument(
        "--crypto-aes-dec-ii",
        default="216",
        type=int,
        help="AES decryption initiation interval in cycles (default: 216)",
    )
    parser.add_argument(
        "--crypto-mac-cycles",
        default="200",
        type=int,
        help="MAC operation delay in cycles (default: 200)",
    )
    parser.add_argument(
        "--crypto-mac-ii",
        default="200",
        type=int,
        help="MAC operation initiation interval in cycles (default: 200)",
    )
    parser.add_argument(
        "--crypto-counter-bits",
        default="56",
        type=int,
        help="Amount of bits per AES counter value (default: 56)",
    )
    parser.add_argument(
        "--crypto-mac-bits",
        default="56",
        type=int,
        help="Amount of bits per MAC value (default: 64)",
    )
    parser.add_argument(
        "--crypto-packing-factor",
        default="8",
        type=int,
        help="Amount counters to group with one MAC for Intel SGX style integrity trees (default: 8)",
    )
    parser.add_argument(
        "--metadata-cache-size",
        default="16KiB",
        help="Metadata cache size (default: 16KiB)",
    )
    parser.add_argument(
        "--metadata-cache-assoc",
        help="Metadata cache associativity (default: 8, should be at least as large as tree height)",
    )
    parser.add_argument(
        "--int-trb-size",
        default=32,
        type=int,
        help="Number of permissible in-flight requests (default: 32, set to -1 for infinite size)",
    )


def create_system(args):
    system = System()

    system.cpu_voltage_domain = VoltageDomain()
    system.clk_domain = SrcClockDomain(
        clock="1GHz",
        voltage_domain=system.cpu_voltage_domain,
    )

    system.mem_mode = "timing"

    n_cores = lambda cpu: [cpu() for _ in range(args.num_cores)]
    if args.ooo:
        system.cpu = n_cores(RiscvO3CPU)
    else:
        system.cpu = n_cores(RiscvTimingSimpleCPU)

    for core in system.cpu:
        core.createInterruptController()

    MemorySystem.initialize(system, args)

    return system


def get_processes(args):
    """Interprets provided args and returns a list of processes"""

    multiprocesses = []
    inputs = []
    outputs = []
    errouts = []
    pargs = []

    workloads = args.cmd.split(";")
    if args.input != "":
        inputs = args.input.split(";")
    if args.output != "":
        outputs = args.output.split(";")
    if args.errout != "":
        errouts = args.errout.split(";")
    if args.options != "":
        pargs = args.options.split(";")

    idx = 0
    for wrkld in workloads:
        process = Process(pid=100 + idx)
        process.executable = wrkld
        process.cwd = os.getcwd()
        process.gid = os.getgid()

        if args.env:
            with open(args.env) as f:
                process.env = [line.rstrip() for line in f]

        if len(pargs) > idx:
            process.cmd = [wrkld] + pargs[idx].split()
        else:
            process.cmd = [wrkld]

        if len(inputs) > idx:
            process.input = inputs[idx]
        if len(outputs) > idx:
            process.output = outputs[idx]
        if len(errouts) > idx:
            process.errout = errouts[idx]

        multiprocesses.append(process)
        idx += 1

    return multiprocesses, idx


def set_threaded_workload(system, args):
    if args.cmd:
        multiprocesses, numThreads = get_processes(args)
    else:
        print("No workload specified. Exiting!\n", file=sys.stderr)
        sys.exit(1)

    # set system workload itself
    mp0_path = multiprocesses[0].executable
    system.workload = SEWorkload.init_compatible(mp0_path)

    # set workload for each cpu
    if len(system.cpu) != len(multiprocesses):
        print(
            "Workloads must match number of processors. Exiting!\n",
            file=sys.stderr,
        )
        sys.exit(1)
    for cpu, process in zip(system.cpu, multiprocesses):
        cpu.workload = process
        if args.maxinsts:
            print(f"limiting execution to {args.maxinsts} instructions")
            cpu.max_insts_any_thread = args.maxinsts
        cpu.createThreads()


def main():
    parser = argparse.ArgumentParser(
        description="A simple system for secure memory experiments."
    )
    add_common_args(parser)
    add_custom_args(parser)
    Options.addSEOptions(parser)
    args = parser.parse_args()
    system = create_system(args)
    config_filesystem(system, args)
    set_threaded_workload(system, args)

    # create root of gem5 hierarchy
    root = Root(full_system=False, system=system)

    m5.instantiate()

    print()
    event = m5.simulate()
    print(f"{event.getCause()} ({event.getCode()}) @ {m5.curTick()}")


if __name__ == "__m5_main__":
    main()
