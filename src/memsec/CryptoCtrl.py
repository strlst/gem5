from m5.objects.ClockedObject import ClockedObject
from m5.params import *


class CryptoCtrl(ClockedObject):
    type = "CryptoCtrl"
    cxx_header = "memsec/crypto_ctrl.hh"
    cxx_class = "gem5::CryptoCtrl"

    total_memory_addresses = Param.Int(0, "total memory size in bytes")
    bus_bytes = Param.Int(0, "bus size in bytes")
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
    counter_bits = Param.Int(56, "AES counter value size in bits")
    mac_bits = Param.Int(64, "MAC resulting size in bits")
    packing_factor = Param.Int(
        8, "amount of counters which form one node (or MAC)"
    )
    # TODO: find practical values for MAC operation durations
    mac_cycles = Param.Cycles(200, "MAC operation cycle delay per block")
    mac_ii = Param.Cycles(200, "MAC operation initiation interval per block")
    tree_node_bytes = Param.Int(0, "integrity tree node size in bytes")
    tree_height = Param.Int(0, "integrity tree layer count")

    # cpu side port, connected to the CPU
    cpu_side_port = ResponsePort("CPU side port")

    # mem side port, connected to the memory controller
    mem_side_port = RequestPort("memory side port")

    # memory range, usually covering the whole memory controller range
    range_total = Param.AddrRange("total memory range")
    range_data = Param.AddrRange("data region memory range")
    range_integrity = Param.AddrRange("integrity region memory range")
    range_leaves = Param.AddrRange("integrity tree leaves region memory range")
