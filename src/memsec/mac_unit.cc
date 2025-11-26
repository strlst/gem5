#include "memsec/mac_unit.hh"

#include <cstdint>

#include "debug/MACUnit.hh"
#include "memsec/util.hh"

namespace gem5
{

MACUnit::MACUnit(const MACUnitParams& params)
    : SimObject(params), count(params.mac_unit_count),
      mac_bits(params.mac_bits), mac_bytes(params.mac_bits / 8),
      mac_cycles(params.mac_cycles), mac_ii(params.mac_ii)
{
    DPRINTF(MACUnit, "\t\t\t%d mac unit count\n", count);
    DPRINTF(MACUnit, "\t\t\t%d mac bits (%d bytes)\n", mac_bits, mac_bytes);
    DPRINTF(MACUnit, "\t\t\t%d mac cycles (%d ii)\n", mac_cycles, mac_ii);
    for (int i = 0; i < count; i++)
        mac_ready.push_back(0);
}

Tick
MACUnit::get_earliest_ready_time(PacketPtr pkt)
{
    uint64_t min_tick = mac_ready[0];
    uint8_t min_index = 0;
    for (int i = 1; i < count; i++) {
        if (mac_ready[i] < min_tick) {
            min_tick = mac_ready[i];
            min_index = i;
        }
    }

    DPRINTF(MACUnit, "schedule on unit %d with min tick %ld\n", min_index,
        min_tick);

    Tick start = curTick() > min_tick ? curTick() : min_tick;
    Tick end = start + CYCLES_TO_TICKS(mac_cycles);
    // consider how long the current operations blocks other
    // incoming requests
    mac_ready[min_index] = start + CYCLES_TO_TICKS(mac_ii);

    return end;
}

}
