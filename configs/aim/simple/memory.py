import math

from caches import *

import m5
from m5.objects import *
from m5.util import fatal

from gem5.components.memory.dramsim_3 import DRAMSim3MemCtrl


class MetadataCache(Cache):
    assoc = 2
    tag_latency = 2
    data_latency = 2
    response_latency = 2
    mshrs = 4
    tgts_per_mshr = 20

    def __init__(
        self,
        size,
        assoc=None,
        tag_latency=None,
        data_latency=None,
        response_latency=None,
        mshrs=None,
        tgts_per_mshr=None,
    ):
        super().__init__()
        self.size = size
        if assoc:
            self.assoc = assoc
        if tag_latency:
            self.tag_latency = tag_latency
        if data_latency:
            self.data_latency = data_latency
        if response_latency:
            self.response_latency = response_latency
        if mshrs:
            self.mshrs = mshrs
        if tgts_per_mshr:
            self.tgts_per_mshr = tgts_per_mshr

    def connectCPUSideBusPort(self, bus_side_port):
        self.cpu_side = bus_side_port

    def connectMemSideBusPort(self, bus_side_port):
        self.mem_side = bus_side_port


class MemorySystem:
    def parameterize_aim(args, system):
        # TODO: remove this hack
        bus_bytes = 64
        range_total = AddrRange(
            Addr(system.mem_ranges[0].start),
            size=system.mem_ranges[0].size(),
        )
        total_memory_bytes = range_total.size()
        p = args.aim_packing_factor

        # get tree node size
        tree_node_bytes = (args.aim_counter_bits * p + args.aim_mac_bits) // 8

        # formula is derived by hand
        # node size s [bits] = p * c + m
        # total size t [bits] >= 128pl + ns
        #            t [bits] >= 128p^(h+1) + s(p^(h+1) - 1)/(p - 1)
        # => h = floor(log_p((t(p - 1) + s) / (128(p - 1) + s))) - 1
        tree_height = math.floor(
            math.log(
                (total_memory_bytes * (p - 1) + tree_node_bytes)
                / (
                    (args.aim_aes_block_bits // 8) * (p**2)
                    + p * (tree_node_bytes - args.aim_dmac_bits // 8)
                    - args.aim_dmac_bits // 8
                )
            )
            / math.log(p)
        )

        # calculate node counts
        tree_node_count = (p ** (tree_height + 1) - 1) // (p - 1)
        leaf_node_count = p**tree_height

        # calculate region sizes in bytes
        # reserve extra space for alignment
        # range_integrity_bytes = tree_node_count * tree_node_bytes
        range_integrity_bytes = (
            int(math.pow(2, math.ceil(math.log2(tree_node_count))))
            * tree_node_bytes
        )
        range_data_bytes = int(total_memory_bytes - range_integrity_bytes)
        non_leaf_bytes = (
            int(tree_node_count - leaf_node_count) * tree_node_bytes
        )
        leaf_bytes = int(leaf_node_count * tree_node_bytes)

        # assign memory regions
        system.aim_ctrl.range_total = range_total
        system.aim_ctrl.range_data = AddrRange(
            Addr(range_total.start),
            size=range_data_bytes,
        )
        system.aim_ctrl.range_integrity = AddrRange(
            Addr(system.aim_ctrl.range_data.end),
            size=range_integrity_bytes,
        )
        system.aim_ctrl.range_leaves = AddrRange(
            Addr(
                system.aim_ctrl.range_integrity.start + (non_leaf_bytes)
                & (-1 - (bus_bytes - 1))
            ),
            size=leaf_bytes,
        )

        # configure aim controller
        system.aim_ctrl.bus_bytes = bus_bytes
        system.aim_ctrl.counter_bits = args.aim_counter_bits
        system.aim_ctrl.tree_node_bytes = tree_node_bytes
        system.aim_ctrl.tree_height = tree_height
        # configure aes unit
        aes_unit = system.aim_ctrl.aes_unit
        aes_unit.aes_enc_cycles = args.aim_aes_enc_cycles
        aes_unit.aes_dec_cycles = args.aim_aes_dec_cycles
        aes_unit.aes_enc_ii = args.aim_aes_enc_ii
        aes_unit.aes_dec_ii = args.aim_aes_dec_ii
        aes_unit.aes_block_bits = args.aim_aes_block_bits
        # configure mac unit
        mac_unit = system.aim_ctrl.mac_unit
        mac_unit.mac_cycles = args.aim_mac_cycles
        mac_unit.mac_ii = args.aim_mac_ii
        mac_unit.mac_bits = args.aim_dmac_bits

        int_trb = system.aim_ctrl.int_trb
        int_trb.size = args.int_trb_size
        int_trb.merge_req = args.int_merge_req
        int_trb.defrag_req = args.int_defrag_req
        int_trb.bus_bytes = bus_bytes
        int_trb.packing_factor = args.aim_packing_factor
        int_trb.counter_bits = args.aim_counter_bits
        int_trb.tree_height = tree_height
        int_trb.tree_node_bytes = tree_node_bytes
        int_trb.range_integrity = system.aim_ctrl.range_integrity
        int_trb.mac_unit.mac_cycles = args.aim_mac_cycles
        int_trb.mac_unit.mac_ii = args.aim_mac_ii
        int_trb.mac_unit.mac_bits = args.aim_mac_bits

        # configure metadata cache
        if args.metadata_cache_assoc:
            system.metadata_cache.assoc = args.metadata_cache_assoc
        else:
            system.metadata_cache.assoc = int(
                2 ** math.ceil(math.log2(tree_height))
            )
        system.metadata_cache.write_allocator.cache_line_size = tree_node_bytes

        # update memory ranges
        system.mem_ranges = [
            system.aim_ctrl.range_data,
            system.aim_ctrl.range_integrity,
        ]

    def initialize(system, args):
        system.l2bus = L2XBar()
        system.l2cache = L2Cache(size=args.l2_size)
        system.l2cache.connectCPUSideBusPort(system.l2bus.mem_side_ports)

        for core in system.cpu:
            core.icache = L1ICache(size=args.l1i_size)
            core.dcache = L1DCache(size=args.l1d_size)
            core.icache.connectCPU(core)
            core.dcache.connectCPU(core)

            core.icache.connectMemSideBusPort(system.l2bus.cpu_side_ports)
            core.dcache.connectMemSideBusPort(system.l2bus.cpu_side_ports)

        if args.l3:
            system.l3cache = L3Cache(size=args.l3_size)
            system.l3cache.connectCPUSideBusPort(system.l2cache.mem_side)
            last_level_cache = system.l3cache
        else:
            last_level_cache = system.l2cache

        # dramsim specific setup
        system.mem_ctrl = DRAMSim3MemCtrl(
            mem_name="DDR4_8Gb_x8_3200", num_chnls=1
        )
        system.mem_ranges = [AddrRange("8GiB")]
        system.mem_ctrl.range = system.mem_ranges[0]

        # interconnects
        system.membus = SystemXBar()
        system.mem_ctrl.port = system.membus.mem_side_ports

        # configure intermediary aim controller if specified
        if args.memsec:
            system.aim_ctrl = AIMCtrl()
            system.metadata_cache = MetadataCache(
                size=args.metadata_cache_size
            )
            MemorySystem.parameterize_aim(args, system)
            system.metadata_cache.connectCPUSideBusPort(
                system.aim_ctrl.int_trb.metadata_cache_side_port
            )
            system.metadata_cache.connectMemSideBusPort(
                system.membus.cpu_side_ports
            )
            system.aim_ctrl.mem_side_port = system.membus.cpu_side_ports
            last_level_cache.mem_side = system.aim_ctrl.cpu_side_port
        else:
            last_level_cache.mem_side = system.membus.cpu_side_ports
