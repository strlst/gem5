import argparse

from caches import *

import m5
from m5.objects import *

from gem5.components.memory.dramsim_3 import (
    DRAMSim3MemCtrl,
    SingleChannelDDR4_2400,
)
from gem5.resources.resource import obtain_resource

parser = argparse.ArgumentParser(
    description="A simple system for secure memory experiments."
)
# parser.add_argument("binary", default="tests/test-progs/hello/bin/riscv/linux/hello", nargs="?", type=str, help="Path to the binary to execute.")
# parser.add_argument("binary", default=obtain_resource(resource_id="riscv-matrix-multiply"), nargs="?", type=str, help="Path to the binary to execute.")
parser.add_argument(
    "--l1i-size",
    default="16KiB",
    help="L1 instruction cache size. Default: 16KiB.",
)
parser.add_argument(
    "--l1d-size",
    default="16KiB",
    help="L1 data cache size. Default: Default: 16KiB.",
)
parser.add_argument(
    "--l2-size", default="512KiB", help="L2 cache size. Default: 256KiB."
)
args = parser.parse_args()

system = System()

system.cpu_voltage_domain = VoltageDomain()
system.clk_domain = SrcClockDomain(
    clock="1GHz",
    voltage_domain=system.cpu_voltage_domain,
)

system.mem_mode = "atomic"
system.mem_ranges = [AddrRange("8GiB")]

# system.cpu = X86TimingSimpleCPU()
system.cpu = RiscvAtomicSimpleCPU()

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

system.cpu.createInterruptController()

system.system_port = system.membus.cpu_side_ports

# dramsim specific setup
system.mem_ctrl = SingleChannelDDR4_2400()
addr_range, port = system.mem_ctrl.get_mem_ports()[0]
system.membus.mem_side_ports = port
system.mem_ranges[0] = addr_range

# post-system setup section

res = obtain_resource("riscv-matrix-multiply").get_local_path()
system.workload = SEWorkload.init_compatible(res)

process = Process()
process.cmd = [res]
system.cpu.workload = process
system.cpu.createThreads()

root = Root(full_system=False, system=system)
m5.instantiate()
exit_event = m5.simulate()

print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")
