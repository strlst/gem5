import m5
from m5.objects import Cache
from m5.util import fatal


class L1Cache(Cache):
    assoc = 4
    tag_latency = 2
    data_latency = 2
    response_latency = 2
    mshrs = 4
    tgts_per_mshr = 20

    def __init__(self):
        super().__init__()

    def connectCPU(self, cpu):
        raise NotImplementedError

    def connectBus(self, bus):
        self.mem_side = bus.cpu_side_ports


class L1ICache(L1Cache):
    def __init__(self, size=None):
        if not size:
            fatal("cannot instantiate L1ICache without size parameter")
        super().__init__()
        self.size = size

    def connectCPU(self, cpu):
        self.cpu_side = cpu.icache_port


class L1DCache(L1Cache):
    def __init__(self, size=None):
        if not size:
            fatal("cannot instantiate L1DCache without size parameter")
        super().__init__()
        self.size = size

    def connectCPU(self, cpu):
        self.cpu_side = cpu.dcache_port


class L2Cache(Cache):
    assoc = 8
    tag_latency = 2
    data_latency = 3
    response_latency = 3
    mshrs = 5
    tgts_per_mshr = 20

    def __init__(self, size=None):
        if not size:
            fatal("cannot instantiate L2Cache without size parameter")
        super().__init__()
        self.size = size

    def connectCPUSideBus(self, bus):
        self.cpu_side = bus.mem_side_ports

    def connectMemSideBus(self, bus):
        self.mem_side = bus.cpu_side_ports
