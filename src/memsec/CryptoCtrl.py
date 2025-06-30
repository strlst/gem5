from m5.objects.ClockedObject import ClockedObject
from m5.params import *


class CryptoCtrl(ClockedObject):
    type = "CryptoCtrl"
    cxx_header = "memsec/crypto_ctrl.hh"
    cxx_class = "gem5::CryptoCtrl"

    # default parameters sourced from
    # https://ieeexplore.ieee.org/document/7019004
    aes_enc_cycles = Param.Cycles(
        336, "AES-CTR encryption operation cycle delay per block"
    )
    aes_dec_cycles = Param.Cycles(
        216, "AES-CTR decryption operation cycle delay per block"
    )
    aes_enc_ii = Param.Cycles(
        336, "AES-CTR encryption operation initiation interval per block"
    )
    aes_dec_ii = Param.Cycles(
        216, "AES-CTR decryption operation initiation interval per block"
    )
    aes_block_bits = Param.Int(128, "AES-CTR block size in bits")
    counter_bits = Param.Int(64, "AES counter value size in bits")
    mac_bits = Param.Int(512, "MAC resulting size in bits")
    packing_factor = Param.Int(
        8, "amount of counters which form one node (or MAC)"
    )

    # cpu side port, connected to the CPU
    cpu_side_port = ResponsePort("CPU side port")

    # mem side port, connected to the memory controller
    mem_side_port = RequestPort("memory side port")

    # memory range, usually covering the whole memory controller range
    range = Param.AddrRange("memory range")
    # the situation is not ideal, but this parameter must correspond to the used
    # dramsim3 config "bus_width" parameter
    bytes_per_address = Param.Int(64, "bytes per address")
