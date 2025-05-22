#include "memsec/crypto_ctrl.hh"

#include "base/trace.hh"
#include "debug/CryptoCtrl.hh"

namespace gem5 {

std::string formattedPacket(PacketPtr pkt) {
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
      memPort(params.name + ".mem_side_port", this), blocked(false) {
    DPRINTF(CryptoCtrl, "crypto controller constructor\n");
}

Port& CryptoCtrl::getPort(const std::string& if_name, PortID idx) {
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

void CryptoCtrl::CPUSidePort::sendPacket(PacketPtr pkt) {
    // Note: this flow control is very simple since the memobj is blocking
    DPRINTF(CryptoCtrl, "send packet %s\n", formattedPacket(pkt));

    panic_if(blockedPacket != nullptr, "should never try to send if blocked!");

    // If we can't send the packet across the port, store it for later.
    if (!sendTimingResp(pkt)) {
        blockedPacket = pkt;
    }
}

AddrRangeList CryptoCtrl::CPUSidePort::getAddrRanges() const {
    return owner->getAddrRanges();
}

void CryptoCtrl::CPUSidePort::trySendRetry() {
    if (needRetry && blockedPacket == nullptr) {
        // Only send a retry if the port is now completely free
        needRetry = false;
        DPRINTF(CryptoCtrl, "sending cpu side retry req for %d\n", id);
        sendRetryReq();
    }
}

void CryptoCtrl::CPUSidePort::recvFunctional(PacketPtr pkt) {
    // Just forward to the memobj.
    return owner->handleFunctional(pkt);
}

bool CryptoCtrl::CPUSidePort::recvTimingReq(PacketPtr pkt) {
    DPRINTF(CryptoCtrl, "received timing request %s, blocked=%d\n",
            formattedPacket(pkt), owner->blocked);
    // just forward
    if (!owner->handleRequest(pkt)) {
        needRetry = true;
        return false;
    } else {
        return true;
    }
}

void CryptoCtrl::CPUSidePort::recvRespRetry() {
    // We should have a blocked packet if this function is called.
    assert(blockedPacket != nullptr);

    DPRINTF(CryptoCtrl, "received response retry %s\n",
            formattedPacket(blockedPacket));

    // Grab the blocked packet.
    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;

    // Try to resend it. It's possible that it fails again.
    sendPacket(pkt);
}

void CryptoCtrl::MemSidePort::sendPacket(PacketPtr pkt) {
    // Note: this flow control is very simple since the memobj is blocking
    DPRINTF(CryptoCtrl, "send %s\n", formattedPacket(pkt));

    panic_if(blockedPacket != nullptr, "should never try to send if blocked!");

    // If we can't send the packet across the port, store it for later.
    if (!sendTimingReq(pkt)) {
        blockedPacket = pkt;
    }
}

bool CryptoCtrl::MemSidePort::recvTimingResp(PacketPtr pkt) {
    // just forward
    DPRINTF(CryptoCtrl, "timing response %s\n", formattedPacket(pkt));
    return owner->handleResponse(pkt);
}

void CryptoCtrl::MemSidePort::recvReqRetry() {
    // We should have a blocked packet if this function is called.
    assert(blockedPacket != nullptr);

    DPRINTF(CryptoCtrl, "got mem side retry request for addr %d\n",
            blockedPacket->getAddr());

    // Grab the blocked packet.
    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;

    // Try to resend it. It's possible that it fails again.
    sendPacket(pkt);
}

void CryptoCtrl::MemSidePort::recvRangeChange() { owner->sendRangeChange(); }

bool CryptoCtrl::handleRequest(PacketPtr pkt) {
    DPRINTF(CryptoCtrl, "got request %s, blocked=%d\n", formattedPacket(pkt),
            blocked);
    if (blocked) {
        // there is currently an outstanding request. stall.
        DPRINTF(CryptoCtrl, "stalling due to outstanding request\n");
        return false;
    }

    // This memobj is now blocked waiting for the response to this packet.
    blocked = true;

    // Simply forward to the memory port
    memPort.sendPacket(pkt);

    return true;
}

bool CryptoCtrl::handleResponse(PacketPtr pkt) {
    assert(blocked);
    DPRINTF(CryptoCtrl, "got response %s\n", formattedPacket(pkt));

    // The packet is now done. We're about to put it in the port, no need for
    // this object to continue to stall.
    // We need to free the resource before sending the packet in case the CPU
    // tries to send another request immediately (e.g., in the same callchain).
    blocked = false;

    // Simply forward to the memory port
    cpuPort.sendPacket(pkt);

    // For each of the cpu ports, if it needs to send a retry, it should do it
    // now since this memory object may be unblocked now.
    cpuPort.trySendRetry();

    return true;
}

void CryptoCtrl::handleFunctional(PacketPtr pkt) {
    // just pass this on to the memory side to handle for now
    memPort.sendFunctional(pkt);
}

AddrRangeList CryptoCtrl::getAddrRanges() const {
    DPRINTF(CryptoCtrl, "sending new ranges\n");
    // just use the same ranges as whatever is on the memory side
    return memPort.getAddrRanges();
}

void CryptoCtrl::sendRangeChange() { cpuPort.sendRangeChange(); }

} // namespace gem5
