from m5.objects.ClockedObject import ClockedObject
from m5.objects.SimObject import SimObject
from m5.params import *
from m5.proxy import *


class IntTRB(SimObject):
    type = "IntTRB"
    cxx_header = "memsec/int_tree.hh"
    cxx_class = "gem5::IntTRB"

    size = Param.Int(
        (
            "amount of ongoing integrity tree update (during memory writes)"
            " requests that can be in-flight at the same time"
        ),
    )
    bus_bytes = Param.Int("bus size in bytes")
    packing_factor = Param.Int(
        "amount of counters which form one node (or MAC)"
    )
    counter_bytes = Param.Int("AES counter value size in bytes")
    tree_height = Param.Int("integrity tree layer count")
    tree_node_bytes = Param.Int("integrity tree node size in bytes")
    range_integrity = Param.AddrRange("integrity region memory range")


class CryptoCtrl(ClockedObject):
    type = "CryptoCtrl"
    cxx_header = "memsec/crypto_ctrl.hh"
    cxx_class = "gem5::CryptoCtrl"

    # reference to parent system
    system = Param.System(Parent.any, "system object")

    aes_enc_cycles = Param.Cycles(
        80, "AES-CTR encryption operation cycle delay per block"
    )
    aes_dec_cycles = Param.Cycles(
        80, "AES-CTR decryption operation cycle delay per block"
    )
    aes_enc_ii = Param.Cycles(
        20, "AES-CTR encryption operation initiation interval per block"
    )
    aes_dec_ii = Param.Cycles(
        20, "AES-CTR decryption operation initiation interval per block"
    )
    aes_block_bits = Param.Int(128, "AES-CTR block size in bits")
    counter_bits = Param.Int(56, "AES counter value size in bits")
    mac_bits = Param.Int(64, "MAC resulting size in bits")
    mac_cycles = Param.Cycles(40, "MAC operation cycle delay per block")
    mac_ii = Param.Cycles(10, "MAC operation initiation interval per block")

    # subcomponent
    int_trb = Param.IntTRB(
        IntTRB(),
        "integrity tree request buffer component",
    )

    total_memory_addresses = Param.Int(0, "total memory size in bytes")
    bus_bytes = Param.Int(0, "bus size in bytes")
    packing_factor = Param.Int(
        8, "amount of counters which form one node (or MAC)"
    )
    tree_node_bytes = Param.Int(0, "integrity tree node size in bytes")
    tree_height = Param.Int(0, "integrity tree layer count")
    int_tree_buffer_size = Param.Int(
        16,
        (
            "amount of ongoing integrity tree requests "
            "(during memory reads and writes)"
            " that can be in-flight at the same time"
        ),
    )

    # cpu side port, connected to the CPU
    cpu_side_port = ResponsePort("CPU side port")

    # mem side port, connected to the memory controller
    mem_side_port = RequestPort("memory side port")

    # metadata cache side port, connected to the special metadata cache
    metadata_cache_side_port = RequestPort("metadata cache side port")

    # memory range, usually covering the whole memory controller range
    range_total = Param.AddrRange("total memory range")
    range_data = Param.AddrRange("data region memory range")
    range_integrity = Param.AddrRange("integrity region memory range")
    range_leaves = Param.AddrRange("integrity tree leaves region memory range")
