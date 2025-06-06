#include "memsec/crypto_ctrl.hh"

#include <cstdint>
#include <queue>
#include <utility>

#include "base/stats/units.hh"
#include "base/trace.hh"
#include "debug/CryptoCtrl.hh"
#include "mem/packet.hh"
#include "sim/cur_tick.hh"
#include "sim/eventq.hh"

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
    : SimObject(params), aes_enc_cycles(params.aes_enc_cycles),
      aes_dec_cycles(params.aes_dec_cycles), aes_enc_ii(params.aes_enc_ii),
      aes_dec_ii(params.aes_dec_ii), stats(this),
      delayResponse(
          [this] {
              handleDelayedResponse();
          }, name()),
      delayRequest(
          [this] {
              handleDelayedRequest();
          }, name()),
      cpuPort(params.name + ".cpu_side_port", this),
      memPort(params.name + ".mem_side_port", this)
{
    DPRINTF(CryptoCtrl,
        "Created crypto controller with properties\n"
        "\t\t\t%d aes encryption cycles (%d ii)\n"
        "\t\t\t%d aes decryption cycles (%d ii)\n",
        params.aes_enc_cycles, params.aes_enc_ii, params.aes_dec_cycles,
        params.aes_dec_ii);
}

unsigned int
CryptoCtrl::numberOutstandingResponses() const
{
    return queueResponse.size();
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
    bool success = sendTimingResp(pkt);
    if (!success) {
        failedPackets.push(pkt);
        ++owner->stats.cpuFailuresCountSend;
    }
    DPRINTF(CryptoCtrl, "sent %s, success=%d\n", formattedPacket(pkt),
        success);
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
    auto failedPkt = failedPackets.front();
    failedPackets.pop();
    // just forward
    DPRINTF(CryptoCtrl, "(retry) timing request %s\n",
        formattedPacket(failedPkt));
    owner->stats.cpuRetryCountSend++;

    bool success = owner->handleResponse(failedPkt);
    panic_if(!success, "(retry) timing request is not allowed to fail!");
}

void
CryptoCtrl::MemSidePort::sendPacket(PacketPtr pkt)
{
    owner->stats.memTotalCountSend++;

    // make sure we cannot miss packets
    bool success = sendTimingReq(pkt);
    if (!success) {
        failedPackets.push(pkt);
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
    auto failedPkt = failedPackets.front();
    failedPackets.pop();
    // just forward
    DPRINTF(CryptoCtrl, "(retry) timing request %s\n",
        formattedPacket(failedPkt));
    owner->stats.memRetryCountSend++;

    bool success = owner->handleRequest(failedPkt);
    panic_if(!success, "(retry) timing request is not allowed to fail!");
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
    // short circuit requests which are actually responses on the bus

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

    int iterations = pkt->getSize() / AES_BLOCK_BYTES;
    // every II we schedule an iteration, considering ramp-up and ramp-down
    Tick delay = iterations * aes_enc_ii + (aes_enc_cycles - aes_enc_ii);

    // push new event onto priority queue
    queueRequest.push(std::make_pair(curTick() + delay, pkt));
    auto next = queueRequest.top();
    if (delayRequest.scheduled())
        reschedule(delayRequest, next.first);
    else
        schedule(delayRequest, next.first);

    return true;
}

bool
CryptoCtrl::handleResponse(PacketPtr pkt)
{
    DPRINTF(CryptoCtrl, "handle response %s\n", formattedPacket(pkt));

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
    Tick delay = iterations * aes_dec_ii + (aes_dec_cycles - aes_dec_ii);

    // push new event onto priority queue
    queueResponse.push(std::make_pair(curTick() + delay, pkt));
    auto next = queueResponse.top();
    if (delayResponse.scheduled())
        reschedule(delayResponse, next.first);
    else
        schedule(delayResponse, next.first);

    return true;
}

void
CryptoCtrl::handleDelayedRequest()
{
    // pop request from queue
    auto request = queueRequest.top();
    // top element should be the earliest event
    // that is to say, the current tick
    assert(request.first == curTick());
    queueRequest.pop();

    // reschedule event for remaining request packets
    if (!queueRequest.empty()) {
        schedule(delayRequest, queueRequest.top().first);
    }

    // process packet
    PacketPtr pkt = request.second;
    DPRINTF(CryptoCtrl, "handling delayed request %s\n", formattedPacket(pkt));
    memPort.sendPacket(pkt);
}

void
CryptoCtrl::handleDelayedResponse()
{
    // pop request from queue
    auto response = queueResponse.top();
    // top element should be the earliest event
    // that is to say, the current tick
    assert(response.first == curTick());
    queueResponse.pop();

    // reschedule event for remaining response packets
    if (!queueResponse.empty()) {
        schedule(delayResponse, queueResponse.top().first);
    }

    // process packet
    PacketPtr pkt = response.second;
    DPRINTF(CryptoCtrl, "handling delayed response %s\n",
        formattedPacket(pkt));
    cpuPort.sendPacket(pkt);
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
