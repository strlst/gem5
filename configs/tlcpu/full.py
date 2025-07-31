import argparse

import m5
from m5.objects import *

from gem5.components.boards.riscv_board import RiscvBoard
from gem5.components.cachehierarchies.classic.private_l1_private_l2_walk_cache_hierarchy import (
    PrivateL1PrivateL2WalkCacheHierarchy,
)
from gem5.components.memory.dramsim_3 import SingleChannel
from gem5.components.processors.cpu_types import CPUTypes
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.isas import ISA
from gem5.resources.resource import (
    DiskImageResource,
    obtain_resource,
)
from gem5.simulate.simulator import Simulator
from gem5.utils.requires import requires


def parse_args():
    parser = argparse.ArgumentParser(
        description="A simple system for secure memory experiments."
    )
    # parser.add_argument(
    # "--image",
    # type=str,
    # default="/opt/spec/cpu2017-1_0_2.iso",
    # help="image to the spec cpu iso to be loaded",
    # )
    # parser.add_argument(
    # "--partition",
    # type=str,
    # required=False,
    # default=None,
    # help='input the root partition of the SPEC disk-image. If the disk is \
    # not partitioned, then pass "".',
    # )
    parser.add_argument(
        "--num-cores",
        type=int,
        default=1,
        nargs="?",
        help="number of CPU cores to instantiate",
    )
    parser.add_argument(
        "--l1i-size",
        type=str,
        default="16KiB",
        help="L1 instruction cache size (default: 16KiB)",
    )
    parser.add_argument(
        "--l1d-size",
        type=str,
        default="16KiB",
        help="L1 data cache size (default: 16KiB)",
    )
    parser.add_argument(
        "--l2-size",
        type=str,
        default="512KiB",
        help="L2 cache size (default: 256KiB)",
    )
    parser.add_argument(
        "--l3-size",
        type=str,
        default="4096KiB",
        help="L3 cache size (default: 4096KiB)",
    )
    parser.add_argument(
        "--l3",
        action=argparse.BooleanOptionalAction,
        help="flag to enable/disable existence of an L3 cache",
    )
    parser.add_argument(
        "--ooo",
        action=argparse.BooleanOptionalAction,
        help="flag to enable/disable use of O3 CPU model",
    )
    parser.add_argument(
        "--memsec",
        action=argparse.BooleanOptionalAction,
        help="flag to enable/disable memory security subsystem used for confidentiality and integrity",
    )
    parser.add_argument(
        "--crypto-aes-block-bits",
        default="128",
        type=int,
        help="AES block size in bits (default: 128)",
    )
    # default parameters sourced from
    # https://ieeexplore.ieee.org/document/7019004
    parser.add_argument(
        "--crypto-aes-enc-cycles",
        default="336",
        help="AES encryption delay in cycles (default: 336)",
    )
    parser.add_argument(
        "--crypto-aes-dec-cycles",
        default="216",
        type=int,
        help="AES decryption delay in cycles (default: 216)",
    )
    parser.add_argument(
        "--crypto-aes-enc-ii",
        default="336",
        type=int,
        help="AES encryption initiation interval in cycles (default: 336)",
    )
    parser.add_argument(
        "--crypto-aes-dec-ii",
        default="216",
        type=int,
        help="AES decryption initiation interval in cycles (default: 216)",
    )
    parser.add_argument(
        "--crypto-mac-cycles",
        default="200",
        type=int,
        help="MAC operation delay in cycles (default: 200)",
    )
    parser.add_argument(
        "--crypto-mac-ii",
        default="200",
        type=int,
        help="MAC operation initiation interval in cycles (default: 200)",
    )
    parser.add_argument(
        "--crypto-counter-bits",
        default="56",
        type=int,
        help="Amount of bits per AES counter value (default: 56)",
    )
    parser.add_argument(
        "--crypto-mac-bits",
        type=int,
        default="56",
        help="Amount of bits per MAC value (default: 64)",
    )
    parser.add_argument(
        "--crypto-packing-factor",
        type=int,
        default="8",
        help="Amount counters to group with one MAC for Intel SGX style integrity trees (default: 8)",
    )
    parser.add_argument(
        "--metadata-cache-size",
        type=str,
        default="16KiB",
        help="Metadata cache size (default: 16KiB)",
    )
    parser.add_argument(
        "--tree-update-buffer-size",
        type=int,
        default=32,
        help="Number of permissible in-flight requests (default: 32)",
    )

    return parser.parse_args()


if __name__ == "__m5_main__":
    requires(isa_required=ISA.RISCV)

    args = parse_args()

    cache_hierarchy = PrivateL1PrivateL2WalkCacheHierarchy(
        l1d_size="32KiB", l1i_size="32KiB", l2_size="512KiB"
    )
    memory = SingleChannel("DDR4_8Gb_x8_3200", size="8GiB")
    processor = SimpleProcessor(
        cpu_type=CPUTypes.O3, isa=ISA.RISCV, num_cores=1
    )
    board = RiscvBoard(
        clk_freq="1GHz",
        processor=processor,
        memory=memory,
        cache_hierarchy=cache_hierarchy,
    )

    board.set_kernel_disk_workload(
        kernel=obtain_resource(
            "riscv-bootloader-vmlinux-5.10", resource_version="1.0.0"
        ),
        disk_image=obtain_resource("riscv-disk-img", resource_version="1.0.0"),
        # disk_image=DiskImageResource(args.image, root_partition=args.partition),
    )

    # finally create simulation and start
    simulator = Simulator(board=board)

    # Note: This simulation will never stop. You can access the terminal upon boot
    # using m5term (`./util/term`): `./m5term localhost <port>`. Note the `<port>`
    # value is obtained from the gem5 terminal stdout. Look out for
    # "system.platform.terminal: Listening for connections on port <port>".
    print("始まります!")
    simulator.run()
