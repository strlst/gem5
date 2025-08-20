#include "memsec/aes_unit.hh"

#include "debug/AESUnit.hh"
#include "memsec/util.hh"

namespace gem5
{

AESUnit::AESUnit(const AESUnitParams& params)
    : SimObject(params), aes_enc_ready(0), aes_dec_ready(0),
      aes_block_bytes(params.aes_block_bits / 8),
      aes_enc_cycles(params.aes_enc_cycles),
      aes_dec_cycles(params.aes_dec_cycles), aes_enc_ii(params.aes_enc_ii),
      aes_dec_ii(params.aes_dec_ii)
{
    DPRINTF(AESUnit, "\t\t\t%d aes block bits (%d bytes)\n", aes_block_bits,
        aes_block_bytes);
    DPRINTF(AESUnit, "\t\t\t%d aes encryption cycles (%d ii)\n",
        aes_enc_cycles, aes_enc_ii);
    DPRINTF(AESUnit, "\t\t\t%d aes decryption cycles (%d ii)\n",
        aes_dec_cycles, aes_dec_ii);
}

Tick
AESUnit::get_earliest_enc_ready_time(PacketPtr pkt)
{
    // consider when the incoming request becomes servicable
    // if the unit might be busy
    Tick start = curTick() > aes_enc_ready ? curTick() : aes_enc_ready;
    int iterations = pkt->getSize() / aes_block_bytes;
    // every II we schedule an iteration, considering ramp-up
    // and ramp-down
    Tick delay = CYCLES_TO_TICKS(iterations * aes_enc_ii +
        (aes_enc_cycles - aes_enc_ii));
    Tick end = start + delay;
    // consider how long the current operations blocks other incoming
    // requests
    aes_enc_ready = start + (iterations - 1) * CYCLES_TO_TICKS(aes_enc_ii);

    return end;
}

Tick
AESUnit::get_earliest_dec_ready_time(PacketPtr pkt)
{
    // consider when the incoming request becomes servicable,
    // if the unit might be busy
    Tick start = curTick() > aes_dec_ready ? curTick() : aes_dec_ready;
    int iterations = pkt->getSize() / aes_block_bytes;
    // every II we schedule an iteration, considering ramp-up and
    // ramp-down
    Tick delay = CYCLES_TO_TICKS(iterations * aes_dec_ii +
        (aes_dec_cycles - aes_dec_ii));
    Tick end = start + delay;
    // consider how long the current operations blocks other incoming
    // requests
    aes_dec_ready = start + (iterations - 1) * CYCLES_TO_TICKS(aes_dec_ii);

    return end;
}

}
