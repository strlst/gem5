import argparse

from caches import *

import m5
from m5.objects import *

from gem5.components.memory.dramsim_3 import DRAMSim3MemCtrl
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
    parser.add_argument("--memsec", action=argparse.BooleanOptionalAction)
    parser.add_argument("--ooo", action=argparse.BooleanOptionalAction)
    return parser.parse_args()


def create_system(args):
    system = System()

    system.cpu_voltage_domain = VoltageDomain()
    system.clk_domain = SrcClockDomain(
        clock="1GHz",
        voltage_domain=system.cpu_voltage_domain,
    )

    system.mem_mode = "timing"
    system.mem_ranges = [AddrRange("8GiB")]

    if args.ooo:
        system.cpu = RiscvO3CPU()
    else:
        system.cpu = RiscvTimingSimpleCPU()

    system.cpu.createInterruptController()
    system.membus = SystemXBar()

    system.cpu.icache = L1ICache(size=args.l1i_size)
    system.cpu.dcache = L1DCache(size=args.l1d_size)
    system.cpu.icache.connectCPU(system.cpu)
    system.cpu.dcache.connectCPU(system.cpu)

    system.l2bus = L2XBar()

    system.cpu.icache.connectBus(system.l2bus)
    system.cpu.dcache.connectBus(system.l2bus)

    system.l2cache = L2Cache(size=args.l2_size)
    system.l2cache.connectCPUSideBus(system.l2bus)
    system.l2cache.connectMemSideBus(system.membus)

    # dramsim specific setup
    system.mem_ctrl = DRAMSim3MemCtrl(mem_name="DDR4_8Gb_x8_3200", num_chnls=1)
    print(args.memsec)
    if args.memsec:
        system.crypto_ctrl = CryptoCtrl()
        system.mem_ctrl.port = system.crypto_ctrl.mem_side_port
        system.crypto_ctrl.cpu_side_port = system.membus.mem_side_ports
    else:
        system.mem_ctrl.port = system.membus.mem_side_ports
    system.mem_ranges = [system.mem_ctrl.range]

    return system


def set_workload(system, args):
    system.workload = SEWorkload.init_compatible(args.binary)
    process = Process()
    process.cmd = [args.binary]
    system.cpu.workload = process
    system.cpu.createThreads()


def main():
    args = parse_args()
    system = create_system(args)
    set_workload(system, args)

    # create root of gem5 hierarchy
    root = Root(full_system=False, system=system)

    m5.instantiate()

    print(f"\nSimulation start")
    event = m5.simulate()
    print(f"{event.getCause()} ({event.getCode()}) @ {m5.curTick()}")


if __name__ == "__m5_main__":
    main()
