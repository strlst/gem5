import math

import m5
from m5.objects import *
from m5.util import fatal

from gem5.components.memory.dramsim_3 import DRAMSim3MemCtrl


class MemorySystem:
    def parameterize_crypto_system(self, args, crypto_ctrl, range_total):
        # TODO: remove this hack
        bus_bytes = 64
        p = args.crypto_packing_factor

        # tally total available memory
        total_memory_addresses = range_total.size()
        total_memory_bytes = total_memory_addresses * bus_bytes

        # get tree node size
        tree_node_bytes = (
            args.crypto_counter_bits * p + args.crypto_mac_bits
        ) // 8

        # formula is derived by hand
        # node size s [bits] = p * c + m
        # total size t [bits] <= 128pl + ns
        #            t [bits] <= 128p^(h+1) + s(p^(h+1) - 1)/(p - 1)
        # => h = ceil(log_p((t(p - 1) + s) / (128(p - 1) + s))) - 1
        tree_height = (
            math.ceil(
                math.log(
                    (total_memory_addresses * (p - 1) + tree_node_bytes)
                    / (
                        (args.crypto_aes_block_bits // 8) * (p - 1)
                        + tree_node_bytes
                    )
                )
                / math.log(p)
            )
            - 1
        )

        # calculate node counts
        tree_node_count = (p ** (tree_height + 1) - 1) // (p - 1)
        leaf_node_count = p**tree_height

        # calculate region sizes in bytes
        # reserve extra space for alignment
        # range_integrity_bytes = tree_node_count * tree_node_bytes
        range_integrity_bytes = int(
            math.pow(
                2, math.ceil(math.log2(tree_node_count * tree_node_bytes))
            )
        )
        range_data_bytes = int(total_memory_bytes - range_integrity_bytes)
        non_leaf_bytes = (
            int(tree_node_count - leaf_node_count) * tree_node_bytes
        )
        leaf_bytes = int(leaf_node_count * tree_node_bytes)

        # assign memory regions
        crypto_ctrl.range_total = range_total
        crypto_ctrl.range_data = AddrRange(
            Addr(range_total.start), size=range_data_bytes // bus_bytes
        )
        crypto_ctrl.range_integrity = AddrRange(
            Addr(crypto_ctrl.range_data.end),
            size=range_integrity_bytes // bus_bytes,
        )
        crypto_ctrl.range_leaves = AddrRange(
            Addr(
                crypto_ctrl.range_integrity.start + non_leaf_bytes // bus_bytes
            ),
            size=leaf_bytes // bus_bytes,
        )

        print(
            crypto_ctrl.range_total.size(),
            crypto_ctrl.range_data.size() + crypto_ctrl.range_integrity.size(),
        )

        crypto_ctrl.total_memory_addresses = total_memory_addresses
        crypto_ctrl.bus_bytes = bus_bytes
        crypto_ctrl.aes_enc_cycles = args.crypto_aes_enc_cycles
        crypto_ctrl.aes_dec_cycles = args.crypto_aes_dec_cycles
        crypto_ctrl.aes_enc_ii = args.crypto_aes_enc_ii
        crypto_ctrl.aes_dec_ii = args.crypto_aes_dec_ii
        crypto_ctrl.mac_cycles = args.crypto_mac_cycles
        crypto_ctrl.mac_ii = args.crypto_mac_ii
        crypto_ctrl.aes_block_bits = args.crypto_aes_block_bits
        crypto_ctrl.counter_bits = args.crypto_counter_bits
        crypto_ctrl.mac_bits = args.crypto_mac_bits
        crypto_ctrl.tree_node_bytes = tree_node_bytes
        crypto_ctrl.tree_height = tree_height

    def initialize(self, system, args, cache_system):
        # dramsim specific setup
        system.mem_ctrl = DRAMSim3MemCtrl(
            mem_name="DDR4_8Gb_x8_3200", num_chnls=1
        )

        # interconnects
        system.mem_bus = SystemXBar()
        system.mem_ctrl.port = system.mem_bus.mem_side_ports

        # ranges
        system.mem_ranges = [system.mem_ctrl.range]

        # configure intermediary crypto controller if specified
        if args.memsec:
            system.crypto_ctrl = CryptoCtrl()
            self.parameterize_crypto_system(
                args, system.crypto_ctrl, system.mem_ctrl.range
            )
            system.crypto_ctrl.mem_side_port = system.mem_bus.cpu_side_ports
            cache_system.connectMemSide(system.crypto_ctrl.cpu_side_port)
        else:
            cache_system.connectMemSide(system.mem_bus.cpu_side_ports)
