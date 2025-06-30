import m5
from m5.objects import *
from m5.util import fatal

from gem5.components.memory.dramsim_3 import DRAMSim3MemCtrl


class MemorySystem:
    def initialize(self, system, args, cache_system):
        # dramsim specific setup
        system.mem_ctrl = DRAMSim3MemCtrl(
            mem_name="DDR4_8Gb_x8_3200", num_chnls=1
        )

        # interconnects
        system.membus = SystemXBar()
        system.mem_ctrl.port = system.membus.mem_side_ports

        # ranges
        system.mem_ranges = [system.mem_ctrl.range]

        # configure intermediary crypto controller if specified
        if args.memsec:
            system.crypto_ctrl = CryptoCtrl()
            system.crypto_ctrl.mem_side_port = system.membus.cpu_side_ports
            system.crypto_ctrl.range = system.mem_ctrl.range
            cache_system.connectMemSide(system.crypto_ctrl.cpu_side_port)
        else:
            cache_system.connectMemSide(system.membus.cpu_side_ports)
