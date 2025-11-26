#ifndef __MEMSEC_MAC_UNIT_H__
#define __MEMSEC_MAC_UNIT_H__

#include "base/types.hh"
#include "mem/packet.hh"
#include "params/MACUnit.hh"
#include "sim/sim_object.hh"

namespace gem5
{

class MACUnit : public SimObject
{
  private:
    // how many MAC processing units are in this unit
    uint8_t count;
    // storing earliest aes ready times
    std::vector<Tick> mac_ready;
    // mac request dimensioning
    uint64_t mac_bits, mac_bytes;
    // mac timing information
    uint64_t mac_cycles, mac_ii;

  public:
    MACUnit(const MACUnitParams& params);

    // time getting functions
    Tick get_earliest_ready_time(PacketPtr pkt);
};

}

#endif // __MEMSEC_MAC_UNIT_H__
