#include "memsec/crypto_ctrl.hh"

#include "base/trace.hh"
#include "debug/CryptoCtrl.hh"
#include "mem/packet.hh"

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
    ss << ")";
    return ss.str();
}

CryptoCtrl::CryptoCtrl(const CryptoCtrlParams& params)
    : SimObject(params), cpuPort(params.name + ".cpu_side_port", this),
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
    // make sure we cannot miss packets
    // assert(failedPkt == nullptr);

    bool success = sendTimingReq(pkt);
    if (!success)
        failedPkt = pkt;
    DPRINTF(CryptoCtrl, "sent %s, success=%d\n", formattedPacket(pkt),
        success);
}

bool
CryptoCtrl::MemSidePort::recvTimingResp(PacketPtr pkt)
{
    // just forward
    DPRINTF(CryptoCtrl, "timing response %s\n", formattedPacket(pkt));
    return owner->handleResponse(pkt);
}

void
CryptoCtrl::MemSidePort::recvReqRetry()
{
    // just forward
    DPRINTF(CryptoCtrl, "(retry) timing request %s\n",
        formattedPacket(failedPkt));
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

    // simply forward to the memory port
    memPort.sendPacket(pkt);

    return true;
}

bool
CryptoCtrl::handleResponse(PacketPtr pkt)
{
    DPRINTF(CryptoCtrl, "handle response %s\n", formattedPacket(pkt));

    // The packet is now done. We're about to put it in the port, no need for
    // this object to continue to stall.
    // We need to free the resource before sending the packet in case the CPU
    // tries to send another request immediately (e.g., in the same callchain).

    // Simply forward to the memory port
    cpuPort.sendPacket(pkt);

    return true;
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
