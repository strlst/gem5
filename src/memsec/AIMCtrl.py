from m5.objects.ClockedObject import ClockedObject
from m5.objects.SimObject import SimObject
from m5.params import *
from m5.proxy import *


class AESUnit(SimObject):
    type = "AESUnit"
    cxx_header = "memsec/aes_unit.hh"
    cxx_class = "gem5::AESUnit"

    aes_unit_count = Param.Int(1, "available AES units")
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


class MACUnit(SimObject):
    type = "MACUnit"
    cxx_header = "memsec/mac_unit.hh"
    cxx_class = "gem5::MACUnit"

    mac_unit_count = Param.Int(1, "available AES units")
    mac_bits = Param.Int(64, "MAC resulting size in bits")
    dmac_bits = Param.Int(64, "Data MAC resulting size in bits")
    mac_cycles = Param.Cycles(40, "MAC operation cycle delay per block")
    mac_ii = Param.Cycles(10, "MAC operation initiation interval per block")


class IntTRB(SimObject):
    type = "IntTRB"
    cxx_header = "memsec/int_tree.hh"
    cxx_class = "gem5::IntTRB"

    # reference to parent system
    system = Param.System(Parent.any, "system object")

    mac_unit = Param.MACUnit(MACUnit(), "MAC component")

    size = Param.Int(
        (
            "amount of ongoing integrity tree update (during memory writes)"
            " requests that can be in-flight at the same time"
        ),
    )
    merge_req = Param.Bool(False, "whether to merge int requests in buffer")
    defrag_req = Param.Bool(
        False,
        "whether to defragment merged int requests in buffer (depends on "
        "`merge_req`)",
    )
    bus_bytes = Param.Int(64, "bus size in bytes")
    packing_factor = Param.Int(
        8, "amount of counters which form one node (or MAC)"
    )
    counter_bits = Param.Int(56, "AES counter value size in bytes")
    tree_height = Param.Int("integrity tree layer count")
    tree_node_bytes = Param.Int("integrity tree node size in bytes")
    range_integrity = Param.AddrRange("integrity region memory range")

    # metadata cache side port, connected to the special metadata cache
    metadata_cache_side_port = RequestPort("metadata cache side port")


class AIMCtrl(ClockedObject):
    type = "AIMCtrl"
    cxx_header = "memsec/aim_ctrl.hh"
    cxx_class = "gem5::AIMCtrl"

    # reference to parent system
    system = Param.System(Parent.any, "system object")

    counter_bits = Param.Int(56, "AES counter value size in bits")

    # subcomponents
    aes_unit = Param.AESUnit(AESUnit(), "AES component")
    mac_unit = Param.MACUnit(MACUnit(), "MAC component")
    int_trb = Param.IntTRB(
        IntTRB(),
        "integrity tree request buffer component",
    )

    total_memory_addresses = Param.Int(0, "total memory size in bytes")
    bus_bytes = Param.Int(64, "bus size in bytes")
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

    # memory range, usually covering the whole memory controller range
    range_total = Param.AddrRange("total memory range")
    range_data = Param.AddrRange("data region memory range")
    range_integrity = Param.AddrRange("integrity region memory range")
    range_leaves = Param.AddrRange("integrity tree leaves region memory range")
