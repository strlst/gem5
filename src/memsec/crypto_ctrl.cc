#include "memsec/crypto_ctrl.hh"

#include "base/trace.hh"
#include "debug/CryptoCtrl.hh"

namespace gem5 {

CryptoCtrl::CryptoCtrl(const CryptoCtrlParams& params)
    : SimObject(params), instPort(params.name + ".inst_port", this),
      dataPort(params.name + ".data_port", this),
      memPort(params.name + ".mem_side", this), blocked(false) {
    DPRINTF(CryptoCtrl, "crypto controller constructor\n");
}

Port& CryptoCtrl::getPort(const std::string& if_name, PortID idx) {
    panic_if(idx != InvalidPortID, "This object doesn't support vector ports");

    // This is the name from the Python SimObject declaration (CryptoCtrl.py)
    if (if_name == "mem_side") {
        return memPort;
    } else if (if_name == "inst_port") {
        return instPort;
    } else if (if_name == "data_port") {
        return dataPort;
    } else {
        // pass it along to our super class
        return SimObject::getPort(if_name, idx);
    }
}

void CryptoCtrl::CPUSidePort::sendPacket(PacketPtr pkt) {
    // Note: This flow control is very simple since the memobj is blocking.

    panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");

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
        DPRINTF(CryptoCtrl, "Sending retry req for %d\n", id);
        sendRetryReq();
    }
}

void CryptoCtrl::CPUSidePort::recvFunctional(PacketPtr pkt) {
    // Just forward to the memobj.
    return owner->handleFunctional(pkt);
}

bool CryptoCtrl::CPUSidePort::recvTimingReq(PacketPtr pkt) {
    // Just forward to the memobj.
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

    // Grab the blocked packet.
    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;

    // Try to resend it. It's possible that it fails again.
    sendPacket(pkt);
}

void CryptoCtrl::MemSidePort::sendPacket(PacketPtr pkt) {
    // Note: This flow control is very simple since the memobj is blocking.

    panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");

    // If we can't send the packet across the port, store it for later.
    if (!sendTimingReq(pkt)) {
        blockedPacket = pkt;
    }
}

bool CryptoCtrl::MemSidePort::recvTimingResp(PacketPtr pkt) {
    // Just forward to the memobj.
    return owner->handleResponse(pkt);
}

void CryptoCtrl::MemSidePort::recvReqRetry() {
    // We should have a blocked packet if this function is called.
    assert(blockedPacket != nullptr);

    // Grab the blocked packet.
    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;

    // Try to resend it. It's possible that it fails again.
    sendPacket(pkt);
}

void CryptoCtrl::MemSidePort::recvRangeChange() { owner->sendRangeChange(); }

bool CryptoCtrl::handleRequest(PacketPtr pkt) {
    if (blocked) {
        // There is currently an outstanding request. Stall.
        return false;
    }

    DPRINTF(CryptoCtrl, "Got request for addr %#x\n", pkt->getAddr());

    // This memobj is now blocked waiting for the response to this packet.
    blocked = true;

    // Simply forward to the memory port
    memPort.sendPacket(pkt);

    return true;
}

bool CryptoCtrl::handleResponse(PacketPtr pkt) {
    assert(blocked);
    DPRINTF(CryptoCtrl, "Got response for addr %#x\n", pkt->getAddr());

    // The packet is now done. We're about to put it in the port, no need for
    // this object to continue to stall.
    // We need to free the resource before sending the packet in case the CPU
    // tries to send another request immediately (e.g., in the same callchain).
    blocked = false;

    // Simply forward to the memory port
    if (pkt->req->isInstFetch()) {
        instPort.sendPacket(pkt);
    } else {
        dataPort.sendPacket(pkt);
    }

    // For each of the cpu ports, if it needs to send a retry, it should do it
    // now since this memory object may be unblocked now.
    instPort.trySendRetry();
    dataPort.trySendRetry();

    return true;
}

void CryptoCtrl::handleFunctional(PacketPtr pkt) {
    // Just pass this on to the memory side to handle for now.
    memPort.sendFunctional(pkt);
}

AddrRangeList CryptoCtrl::getAddrRanges() const {
    DPRINTF(CryptoCtrl, "Sending new ranges\n");
    // Just use the same ranges as whatever is on the memory side.
    return memPort.getAddrRanges();
}

void CryptoCtrl::sendRangeChange() {
    instPort.sendRangeChange();
    dataPort.sendRangeChange();
}

} // namespace gem5
