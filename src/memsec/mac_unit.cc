#include "memsec/mac_unit.hh"

#include "debug/MACUnit.hh"
#include "memsec/util.hh"

namespace gem5
{

MACUnit::MACUnit(const MACUnitParams& params)
    : SimObject(params), mac_ready(0), mac_bytes(params.mac_bits / 8),
      mac_cycles(params.mac_cycles), mac_ii(params.mac_ii)
{
    DPRINTF(MACUnit, "\t\t\t%d mac bits (%d bytes)\n", mac_bits, mac_bytes);
    DPRINTF(MACUnit, "\t\t\t%d mac cycles (%d ii)\n", mac_cycles, mac_ii);
}

Tick
MACUnit::get_earliest_ready_time(PacketPtr pkt)
{
    Tick start = curTick() > mac_ready ? curTick() : mac_ready;
    Tick end = start + CYCLES_TO_TICKS(mac_cycles);
    // consider how long the current operations blocks other
    // incoming requests
    mac_ready = start + CYCLES_TO_TICKS(mac_ii);

    return end;
}

}
