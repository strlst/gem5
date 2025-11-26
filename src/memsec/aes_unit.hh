#ifndef __MEMSEC_AES_UNIT_H__
#define __MEMSEC_AES_UNIT_H__

#include "base/types.hh"
#include "mem/packet.hh"
#include "params/AESUnit.hh"
#include "sim/sim_object.hh"

namespace gem5
{

class AESUnit : public SimObject
{
  private:
    // how many AES processing units are in this unit
    uint8_t count;
    // storing earliest mac ready times
    std::vector<Tick> aes_ready;
    // aes request dimensioning
    uint64_t aes_block_bits, aes_block_bytes;
    // aes timing information
    uint64_t aes_enc_cycles, aes_dec_cycles;
    uint64_t aes_enc_ii, aes_dec_ii;
  public:
    AESUnit(const AESUnitParams& params);

    // time getting functions
    Tick get_earliest_enc_ready_time(PacketPtr ptr);
    Tick get_earliest_dec_ready_time(PacketPtr pkt);
};

}

#endif // __MEMSEC_AES_UNIT_H__
