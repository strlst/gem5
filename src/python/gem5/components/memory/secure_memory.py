from typing import (
    List,
    Sequence,
    Tuple,
)

import m5
from m5.objects import (
    AddrRange,
    MemCtrl,
    Port,
)

from ...utils.override import overrides
from ..boards.abstract_board import AbstractBoard
from .abstract_memory_system import AbstractMemorySystem
from .dramsim_3 import SingleChannel


class SecureMemory(AbstractMemorySystem):
    def __init__(self, args, dram_mem_type: str, dram_size: str):
        """
        :param dram_mem_name:
            The name of the type  of memory to be configured.
        :param dram_num_chnls: The number of channels.
        """
        super().__init__()
        self.mem_sys = SingleChannel(mem_type=dram_mem_type, size=dram_size)
        print(vars(self.mem_sys))
        # TODO:
        # actually incorporate secure memory system

    @overrides(AbstractMemorySystem)
    def incorporate_memory(self, board: AbstractBoard) -> None:
        self.mem_sys.incorporate_memory(board)

    @overrides(AbstractMemorySystem)
    def get_mem_ports(self) -> Tuple[Sequence[AddrRange], Port]:
        return self.mem_sys.get_mem_ports()

    @overrides(AbstractMemorySystem)
    def get_memory_controllers(self) -> List[MemCtrl]:
        return self.mem_sys.get_memory_controllers()

    @overrides(AbstractMemorySystem)
    def get_size(self) -> int:
        return self.mem_sys.get_size()

    @overrides(AbstractMemorySystem)
    def set_memory_range(self, ranges: List[AddrRange]) -> None:
        self.mem_sys.set_memory_range(ranges)
