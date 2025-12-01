#ifndef __MEMSEC_UTIL_HH__
#define __MEMSEC_UTIL_HH__

#include <string>

#include "mem/packet.hh"
#include "mem/request.hh"

namespace gem5
{

#define TICK_PER_CYCLE 1000
#define AFTER_1_CYCLE(t) ((t) + TICK_PER_CYCLE)
#define AFTER_N_CYCLES(t, n) ((t) + ((n) * TICK_PER_CYCLE))
#define CYCLES_TO_TICKS(n) ((n) * TICK_PER_CYCLE)

inline PacketPtr
createPkt(Addr addr, size_t size, RequestorID requestorId, MemCmd cmd)
{
    Request::Flags flags = Request::PHYSICAL;
    // we simply create a new packet and fill it, first creating a
    // req pointer and finally a packet ptr
    RequestPtr request =
        std::make_shared<Request>(addr, size, flags, requestorId);
    PacketPtr pkt = new Packet(request, cmd);
    // create uninitialized data
    pkt->allocate();
    return pkt;
}

inline PacketPtr
createPktFromPkt(PacketPtr pkt, MemCmd cmd)
{
    PacketPtr new_pkt =
        createPkt(pkt->getAddr(), pkt->getSize(), pkt->requestorId(), cmd);
    memcpy(new_pkt->getPtr<uint8_t>(), pkt->getPtr<uint8_t>(), pkt->getSize());
    return new_pkt;
}

inline std::string
formattedPacket(PacketPtr pkt)
{
    std::ostringstream ss;
    ss << "pkt(";
    ss << "addr=0x" << std::hex << pkt->getAddr() << std::dec;
    ss << ", id=" << pkt->id;
    ss << ", cmd=" << pkt->cmdString();
    ss << ", size=" << unsigned(pkt->getSize());
    ss << ", read=" << unsigned(pkt->isRead());
    ss << ", reqid=" << unsigned(pkt->requestorId());
    //ss << ", flags=0x" << std::hex << unsigned(pkt->req->getFlags())
    //<< std::dec;
    ss << ", hasdata=" << unsigned(pkt->hasData());
    ss << ")";
    return ss.str();
}


}

#endif // __MEMSEC_UTIL_HH__
