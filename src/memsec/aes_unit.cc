#include "memsec/aes_unit.hh"

#include "debug/AESUnit.hh"
#include "memsec/util.hh"

namespace gem5
{

AESUnit::AESUnit(const AESUnitParams& params)
    : SimObject(params), count(params.aes_unit_count), aes_ready(0),
      aes_block_bytes(params.aes_block_bits / 8),
      aes_enc_cycles(params.aes_enc_cycles),
      aes_dec_cycles(params.aes_dec_cycles), aes_enc_ii(params.aes_enc_ii),
      aes_dec_ii(params.aes_dec_ii)
{
    DPRINTF(AESUnit, "\t\t\t%d aes unit count\n", count);
    DPRINTF(AESUnit, "\t\t\t%d aes block bits (%d bytes)\n", aes_block_bits,
        aes_block_bytes);
    DPRINTF(AESUnit, "\t\t\t%d aes encryption cycles (%d ii)\n",
        aes_enc_cycles, aes_enc_ii);
    DPRINTF(AESUnit, "\t\t\t%d aes decryption cycles (%d ii)\n",
        aes_dec_cycles, aes_dec_ii);
    for (int i = 0; i < count; i++)
        aes_ready.push_back(0);
}

Tick
AESUnit::get_earliest_enc_ready_time(PacketPtr pkt)
{
    uint64_t min_tick = aes_ready[0];
    uint8_t min_index = 0;
    for (int i = 1; i < count; i++) {
        if (aes_ready[i] < min_tick) {
            min_tick = aes_ready[i];
            min_index = i;
        }
    }

    DPRINTF(AESUnit, "schedule encryption on unit %d with min tick %ld\n",
        min_index, min_tick);

    // consider when the incoming request becomes servicable
    // if the unit might be busy
    Tick start = curTick() > min_tick ? curTick() : min_tick;
    int iterations = pkt->getSize() / aes_block_bytes;
    // every II we schedule an iteration, considering ramp-up
    // and ramp-down
    Tick delay = CYCLES_TO_TICKS(iterations * aes_enc_ii +
        (aes_enc_cycles - aes_enc_ii));
    Tick end = start + delay;
    // consider how long the current operations blocks other incoming
    // requests
    aes_ready[min_index] =
        start + (iterations - 1) * CYCLES_TO_TICKS(aes_enc_ii);

    return end;
}

Tick
AESUnit::get_earliest_dec_ready_time(PacketPtr pkt)
{
    uint64_t min_tick = aes_ready[0];
    uint8_t min_index = 0;
    for (int i = 1; i < count; i++) {
        if (aes_ready[i] < min_tick) {
            min_tick = aes_ready[i];
            min_index = i;
        }
    }

    DPRINTF(AESUnit, "schedule decryption on unit %d with min tick %ld\n",
        min_index, min_tick);

    // consider when the incoming request becomes servicable,
    // if the unit might be busy
    Tick start = curTick() > min_tick ? curTick() : min_tick;
    int iterations = pkt->getSize() / aes_block_bytes;
    // every II we schedule an iteration, considering ramp-up and
    // ramp-down
    Tick delay = CYCLES_TO_TICKS(iterations * aes_dec_ii +
        (aes_dec_cycles - aes_dec_ii));
    Tick end = start + delay;
    // consider how long the current operations blocks other incoming
    // requests
    aes_ready[min_index] =
        start + (iterations - 1) * CYCLES_TO_TICKS(aes_dec_ii);

    return end;
}

}
