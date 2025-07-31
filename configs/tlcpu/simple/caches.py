import m5
from m5.objects import *
from m5.util import fatal


class CacheSystem:
    """This class wraps creation of a multi-level cache hierarchy"""

    def initialize(self, system, args):
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
            self.last_level_cache = system.l3cache
        else:
            self.last_level_cache = system.l2cache

    def connectMemSide(self, cpu_side_port):
        self.last_level_cache.mem_side = cpu_side_port


class BasicCache(Cache):
    assoc = 4
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


class L1Cache(BasicCache):
    assoc = 2
    tag_latency = 2
    data_latency = 2
    response_latency = 2
    mshrs = 4
    tgts_per_mshr = 20


class L1ICache(L1Cache):
    def connectCPU(self, cpu):
        self.cpu_side = cpu.icache_port


class L1DCache(L1Cache):
    def connectCPU(self, cpu):
        self.cpu_side = cpu.dcache_port


class L2Cache(BasicCache):
    assoc = 8
    tag_latency = 16
    data_latency = 16
    response_latency = 16
    mshrs = 20
    tgts_per_mshr = 12
    write_buffers = 8


class L3Cache(BasicCache):
    assoc = 8
    tag_latency = 50
    data_latency = 50
    response_latency = 50
    mshrs = 20
    tgts_per_mshr = 12
