import argparse

from caches import *
from memory import *

import m5
from m5.objects import *

from gem5.resources.resource import obtain_resource


def parse_args():
    parser = argparse.ArgumentParser(
        description="A simple system for secure memory experiments."
    )
    parser.add_argument(
        "binary",
        default="tests/test-progs/matmul/bin/riscv/linux/matmul",
        nargs="?",
        help="path to the binary to use as a bare-metal test",
    )
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

    return parser.parse_args()


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

    cache_system = CacheSystem()
    cache_system.initialize(system, args)

    memory_system = MemorySystem()
    memory_system.initialize(system, args, cache_system)

    return system


def set_threaded_workload(system, args):
    process = Process()
    process.cmd = [args.binary]

    # set workload for each cpu
    for cpu in system.cpu:
        cpu.workload = process
        cpu.createThreads()

    # set system workload itself
    system.workload = SEWorkload.init_compatible(args.binary)


def main():
    args = parse_args()
    system = create_system(args)
    set_threaded_workload(system, args)

    # create root of gem5 hierarchy
    root = Root(full_system=False, system=system)

    m5.instantiate()

    print()
    event = m5.simulate()
    print(f"{event.getCause()} ({event.getCode()}) @ {m5.curTick()}")


if __name__ == "__m5_main__":
    main()
