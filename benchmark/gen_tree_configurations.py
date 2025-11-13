#!/usr/bin/env python3
import argparse
import itertools
import math
from dataclasses import dataclass


@dataclass
class AddrRange:
    start: int
    size: int
    end: int

    def formatted(self):
        return f"[{self.start:#{11}x}:{self.start + self.size - 1:#{11}x}] ({round(self.size / 1024**3):#2} GiB)"

    def gib(self):
        return round(self.size / 1024**3)

    def __init__(self, start: int, size: int):
        self.start = start
        self.size = size
        self.end = start + size

    def __str__(self):
        return f"[{self.start:x}:{self.start + self.size - 1:x}] ({self.gib()} GiB)"


@dataclass
class Args:
    total_memory_bytes: int = 8 * 1024**3
    bus_bytes: int = 64
    arity: int = 8  # packing factor p
    counter_bits: int = 56
    mac_bits: int = 64
    aes_block_bits: int = 128
    dmac_bits: int = 64


@dataclass
class Result:
    range_total: AddrRange
    range_data: AddrRange
    range_integrity: AddrRange
    range_leaves: AddrRange
    overhead: float
    tree_height: int


def configure(args: Args):
    range_total = AddrRange(0, size=args.total_memory_bytes - 1)  # 8 GB

    # get tree node size
    tree_node_bytes = (args.counter_bits * args.arity + args.mac_bits) // 8

    # formula is derived by hand
    # node size s [bits] = p * c + m
    # total size t [bits] >= 128pl + ns
    #            t [bits] >= 128p^(h+1) + s(p^(h+1) - 1)/(p - 1)
    # => h = floor(log_p((t(p - 1) + s) / (128(p - 1) + s))) - 1
    tree_height = math.floor(
        math.log(
            (args.total_memory_bytes * (args.arity - 1) + tree_node_bytes)
            / (
                (args.aes_block_bits // 8) * (args.arity**2)
                + args.arity * (tree_node_bytes - args.dmac_bits // 8)
                - args.dmac_bits // 8
            )
        )
        / math.log(args.arity)
    )

    # calculate node counts
    tree_node_count = (args.arity ** (tree_height + 1) - 1) // (args.arity - 1)
    leaf_node_count = args.arity**tree_height

    # calculate region sizes in bytes
    # reserve extra space for alignment
    # range_integrity_bytes = tree_node_count * tree_node_bytes
    range_integrity_bytes = (
        int(math.pow(2, math.ceil(math.log2(tree_node_count))))
        * tree_node_bytes
    )
    range_data_bytes = int(args.total_memory_bytes - range_integrity_bytes)
    non_leaf_bytes = int(tree_node_count - leaf_node_count) * tree_node_bytes
    leaf_bytes = int(leaf_node_count * tree_node_bytes)

    # assign memory regions
    range_data = AddrRange(
        range_total.start,
        size=range_data_bytes,
    )
    range_integrity = AddrRange(
        range_data.end,
        size=range_integrity_bytes,
    )
    range_leaves = AddrRange(
        range_integrity.start + (non_leaf_bytes) & (-1 - (args.bus_bytes - 1)),
        size=leaf_bytes,
    )

    overhead = round(range_integrity.size * 100 / range_total.size, 2)

    return Result(
        range_total=range_total,
        range_data=range_data,
        range_integrity=range_integrity,
        range_leaves=range_leaves,
        overhead=overhead,
        tree_height=tree_height,
    )


latex_table_preamble = f"""
\\begin{{table}}[ht]
  \\centering
  \\begin{{tabular}}{{<>}}
    \\toprule"""
latex_table_postamble = f"""    \\bottomrule
  \\end{{tabular}}
  \\caption[Counter Tree Design Space Exploration]{{Various configurations and their resulting sizes of data and integrity regions, percentual overhead as well as tree height.}}
  \\label{{tab:int-tree-configurations}}
\\end{{table}}"""


def main(cmd_args):
    node_configurations = (
        (16, 28, 64),
        # (8, 57, 56),
        (8, 56, 64),
        (4, 112, 64),
    )
    total_memory_sizes = (8, 16, 32, 64)
    configurations = list()

    for i, ((p, c, m), t) in enumerate(
        itertools.product(node_configurations, total_memory_sizes)
    ):
        args = Args(
            total_memory_bytes=t * 1024**3,
            arity=p,
            counter_bits=c,
            mac_bits=m,
            dmac_bits=64,
        )
        result = configure(args)
        configurations.append((i, args, result))
        if not cmd_args.latex:
            print(
                f"total={result.range_total.formatted()}, data={result.range_data.formatted()}, integrity={result.range_integrity.formatted()}, overhead={result.overhead}, height={result.tree_height}"
            )
    if cmd_args.latex:
        # memory [GiB] & bus [bytes] & arity & counter [bits] & mac [bits] & aes block [bits] & total [range] & data [range] & integrity [range] & leaves [range] & overhead [\\%] & tree height \\\\
        table = {
            "total_size": ([], "memory [GB]"),
            "bus": ([], "bus [bytes]"),
            "arity": ([], "arity"),
            "counter": ([], "counter [bits]"),
            "mac": ([], "mac [bits]"),
            "aes": ([], "aes block [bits]"),
            "dmac": ([], "dmac [bits]"),
            "range_total": ([], "total [GiB]"),
            "range_data": ([], "data [GiB]"),
            "range_integrity": ([], "integrity [GiB]"),
            "range_leaves": ([], "leaves [GiB]"),
            "overhead": ([], "overhead [\\%]"),
            "height": ([], "tree height"),
        }
        for i, args, result in configurations:
            table["total_size"][0].append(
                round(args.total_memory_bytes / 1024**3)
            ),
            table["bus"][0].append(args.bus_bytes),
            table["arity"][0].append(args.arity),
            table["counter"][0].append(args.counter_bits),
            table["mac"][0].append(args.mac_bits),
            table["aes"][0].append(args.aes_block_bits),
            table["dmac"][0].append(args.dmac_bits),
            table["range_total"][0].append(result.range_total.gib()),
            table["range_data"][0].append(result.range_data.gib()),
            table["range_integrity"][0].append(result.range_integrity.gib()),
            table["range_leaves"][0].append(result.range_leaves.gib()),
            table["overhead"][0].append(result.overhead),
            table["height"][0].append(result.tree_height),
        if cmd_args.latex:
            print(latex_table_preamble.replace("<>", f'l|{"c" * (i + 1)}'))
        for key in table:
            values, description = table[key]
            print(f"    {description}", end="")
            for value in values:
                print(f" & {value}", end="")
            print(" \\\\")
        print(latex_table_postamble)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "-l",
        "--latex",
        action=argparse.BooleanOptionalAction,
        help="Output latex table",
    )
    cmd_args = parser.parse_args()
    main(cmd_args)
