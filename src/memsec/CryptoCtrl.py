from m5.params import *
from m5.SimObject import SimObject


class CryptoCtrl(SimObject):
    type = "CryptoCtrl"
    cxx_header = "memsec/crypto_ctrl.hh"
    cxx_class = "gem5::CryptoCtrl"

    static_latency = Param.Latency("constant delay of crypto unit")

    cpu_side_port = ResponsePort("CPU side port")
    mem_side_port = RequestPort("memory side port")
