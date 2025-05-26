#include "memsec/crypto_ctrl.hh"

#include <cstdint>

#include "base/trace.hh"
#include "debug/CryptoCtrl.hh"
#include "sim/cur_tick.hh"

namespace gem5
{

std::string
formattedPacket(PacketPtr pkt)
{
    std::ostringstream ss;
    ss << "pkt(";
    ss << "addr=" << unsigned(pkt->getAddr());
    ss << ", cmd=" << pkt->cmdString();
    ss << ", id=" << unsigned(pkt->requestorId());
    ss << ", size=" << unsigned(pkt->getSize());
    ss << ")";
    return ss.str();
}

CryptoCtrl::CryptoCtrl(const CryptoCtrlParams& params)
    : SimObject(params), stats(this),
      delayResponse(
          [this] {
              handleDelayedResponse();
          }, name()),
      responsePkt(nullptr), cpuPort(params.name + ".cpu_side_port", this),
      memPort(params.name + ".mem_side_port", this)
{
    DPRINTF(CryptoCtrl, "crypto controller constructor\n");
}

Port&
CryptoCtrl::getPort(const std::string& if_name, PortID idx)
{
    panic_if(idx != InvalidPortID, "This object doesn't support vector ports");

    // This is the name from the Python SimObject declaration (CryptoCtrl.py)
    if (if_name == "mem_side_port") {
        return memPort;
    } else if (if_name == "cpu_side_port") {
        return cpuPort;
    } else {
        // pass it along to our super class
        return SimObject::getPort(if_name, idx);
    }
}

AddrRangeList
CryptoCtrl::CPUSidePort::getAddrRanges() const
{
    return owner->getAddrRanges();
}

void
CryptoCtrl::CPUSidePort::sendPacket(PacketPtr pkt)
{
    DPRINTF(CryptoCtrl, "send %s\n", formattedPacket(pkt));
    ++owner->stats.cpuTotalCountSend;

    // panic if we cannot send packet
    panic_if(!sendTimingResp(pkt), "cannot send packet!");
}

void
CryptoCtrl::CPUSidePort::recvFunctional(PacketPtr pkt)
{
    // just forward
    return owner->handleFunctional(pkt);
}

bool
CryptoCtrl::CPUSidePort::recvTimingReq(PacketPtr pkt)
{
    DPRINTF(CryptoCtrl, "received timing request %s\n", formattedPacket(pkt));
    owner->stats.cpuTotalCountRecv++;

    // just forward
    owner->handleRequest(pkt);

    return true;
}

void
CryptoCtrl::CPUSidePort::recvRespRetry()
{
    panic("this function should never be called!");
}

void
CryptoCtrl::MemSidePort::sendPacket(PacketPtr pkt)
{
    owner->stats.memTotalCountSend++;

    // make sure we cannot miss packets
    bool success = sendTimingReq(pkt);
    if (!success) {
        failedPkt = pkt;
        ++owner->stats.memFailuresCountSend;
    }
    DPRINTF(CryptoCtrl, "sent %s, success=%d\n", formattedPacket(pkt),
        success);
}

bool
CryptoCtrl::MemSidePort::recvTimingResp(PacketPtr pkt)
{
    DPRINTF(CryptoCtrl, "timing response %s\n", formattedPacket(pkt));
    owner->stats.memTotalCountRecv++;

    // just forward
    return owner->handleResponse(pkt);
}

void
CryptoCtrl::MemSidePort::recvReqRetry()
{
    // just forward
    DPRINTF(CryptoCtrl, "(retry) timing request %s\n",
        formattedPacket(failedPkt));
    owner->stats.memRetryCountSend++;

    bool success = owner->handleRequest(failedPkt);
    panic_if(!success, "(retry) timing request is not allowed to fail!");
    failedPkt = nullptr;
}

void
CryptoCtrl::MemSidePort::recvRangeChange()
{
    owner->sendRangeChange();
}

bool
CryptoCtrl::handleRequest(PacketPtr pkt)
{
    DPRINTF(CryptoCtrl, "handle request %s\n", formattedPacket(pkt));
    if (pkt->isRead()) {
        stats.reads++;
    } else {
        stats.writes++;
        /*
        // assume multiples of 64 bytes
        const uint64_t mask = 0xFFFFFFFFFFFFFFFF;
        if (pkt->hasData()) {
            DPRINTF(CryptoCtrl, "XORing write data at %d\n", pkt->getAddr());
            uint64_t* data = pkt->getPtr<uint64_t>();
            for (int i = 0; i < pkt->getSize() / 8; i++) {
                *data = (*data) ^ mask;
                data++;
            }
        }
        */
    }

    // simply forward to the memory port
    memPort.sendPacket(pkt);

    return true;
}

bool
CryptoCtrl::handleResponse(PacketPtr pkt)
{
    DPRINTF(CryptoCtrl, "handle response %s\n", formattedPacket(pkt));
    assert(responsePkt == nullptr);

    /*
    // print data
    uint8_t* byte = pkt->getPtr<uint8_t>();
    for (int i = 0; i < pkt->getSize(); i++) {
        printf("%.02x", *(byte++));
    }
    printf("\n");
    */
    /*
    // assume multiples of 64 bytes
    const uint64_t mask = 0xFFFFFFFFFFFFFFFF;
    if (pkt->hasRespData()) {
        DPRINTF(CryptoCtrl, "XORing read data at %d %d %d\n", pkt->getAddr(),
            pkt->hasData(), pkt->hasRespData());
        uint64_t* data = pkt->getPtr<uint64_t>();
        for (int i = 0; i < pkt->getSize() / 8; i++) {
            *data = (*data) ^ mask;
            data++;
        }
    }
    */

    int iterations = pkt->getSize() / AES_BLOCK_BYTES;
    // every II we schedule an iteration, considering ramp-up and ramp-down
    Tick delay = iterations * AES_DEC_II + (AES_DEC_CYCLES - AES_DEC_II);

    responsePkt = pkt;
    schedule(delayResponse, curTick() + delay);

    return true;
}

void
CryptoCtrl::handleDelayedResponse()
{
    DPRINTF(CryptoCtrl, "handling delayed response %s\n",
        formattedPacket(responsePkt));
    assert(responsePkt != nullptr);

    cpuPort.sendPacket(responsePkt);
    responsePkt = nullptr;
}


void
CryptoCtrl::handleFunctional(PacketPtr pkt)
{
    // just pass this on to the memory side to handle for now
    memPort.sendFunctional(pkt);
}

AddrRangeList
CryptoCtrl::getAddrRanges() const
{
    DPRINTF(CryptoCtrl, "sending new ranges\n");
    // just use the same ranges as whatever is on the memory side
    return memPort.getAddrRanges();
}

void
CryptoCtrl::sendRangeChange()
{
    cpuPort.sendRangeChange();
}

PacketPtr
CryptoCtrl::createPkt(Addr addr, size_t size, uint32_t flags,
    uint16_t requestorId, MemCmd cmd)
{
    // we simply create a new packet and fill it, first creating a
    // req pointer and finally a packet ptr
    RequestPtr req(new Request(addr, size, flags, requestorId));
    PacketPtr newPkt = new Packet(req, cmd);

    // create uninitialized data
    uint8_t* reqData = new uint8_t[size];
    newPkt->dataDynamic(reqData);

    return newPkt;
}

PacketPtr
CryptoCtrl::createPktFromPkt(PacketPtr pkt, MemCmd cmd)
{
    return createPkt(pkt->getAddr(), pkt->getSize(), pkt->req->getFlags(),
        pkt->req->requestorId(), cmd);
}

} // namespace gem5
