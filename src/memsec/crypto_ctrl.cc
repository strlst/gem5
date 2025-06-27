#include "memsec/crypto_ctrl.hh"

#include <cmath>
#include <cstdint>
#include <queue>

#include "base/trace.hh"
#include "crypto_event.hh"
#include "debug/CryptoCtrl.hh"
#include "mem/packet.hh"
#include "sim/clocked_object.hh"
#include "sim/cur_tick.hh"

namespace gem5
{

std::string
formattedPacket(PacketPtr pkt)
{
    std::ostringstream ss;
    ss << "pkt(";
    ss << "addr=0x" << std::hex << unsigned(pkt->getAddr());
    ss << ", cmd=" << pkt->cmdString();
    ss << ", id=" << unsigned(pkt->requestorId());
    ss << ", size=" << unsigned(pkt->getSize());
    ss << ")";
    return ss.str();
}

PacketPtr
createPkt(Addr addr, size_t size, uint32_t flags, uint16_t requestorId,
    MemCmd cmd)
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
createPktFromPkt(PacketPtr pkt, MemCmd cmd)
{
    return createPkt(pkt->getAddr(), pkt->getSize(), pkt->req->getFlags(),
        pkt->req->requestorId(), cmd);
}

CryptoCtrl::CryptoCtrl(const CryptoCtrlParams& params)
    : ClockedObject(params), aes_enc_ready(0), aes_dec_ready(0),
      aes_block_size(params.aes_block_size),
      aes_block_bytes(params.aes_block_size / 8),
      aes_enc_cycles(params.aes_enc_cycles),
      aes_dec_cycles(params.aes_dec_cycles), aes_enc_ii(params.aes_enc_ii),
      aes_dec_ii(params.aes_dec_ii), counter_size(params.counter_size),
      counter_bytes(params.counter_size / 8), mac_size(params.mac_size),
      mac_bytes(params.mac_size / 8),
      mac_packing_factor(params.mac_packing_factor), stats(this),
      cpuPort(params.name + ".cpu_side_port", this),
      memPort(params.name + ".mem_side_port", this)
{
    uint64_t total_memory_size = (uint32_t)exp2(33);
    uint32_t tree_node_size = counter_size + mac_size / mac_packing_factor;

    DPRINTF(CryptoCtrl, "Created crypto controller with properties\n");
    DPRINTF(CryptoCtrl, "\t\t\t%d total memory size (%f MiB)\n",
        total_memory_size, (float)total_memory_size / 8.f / 1024.f / 1024.f);
    DPRINTF(CryptoCtrl, "\t\t\t%d aes block size (%d bytes)\n",
        aes_block_size, aes_block_bytes);
    DPRINTF(CryptoCtrl, "\t\t\t%d aes encryption cycles (%d ii)\n",
        aes_enc_cycles, aes_enc_ii);
    DPRINTF(CryptoCtrl, "\t\t\t%d aes decryption cycles (%d ii)\n",
        aes_dec_cycles, aes_dec_ii);
    DPRINTF(CryptoCtrl, "\t\t\t%d counter size (%d bytes)\n", counter_size,
        counter_bytes);
    DPRINTF(CryptoCtrl, "\t\t\t%d mac size (%d bytes, %d packing factor)\n",
        mac_size, mac_bytes, mac_packing_factor);
    DPRINTF(CryptoCtrl, "\t\t\t%d tree node size (%d bytes)\n",
        tree_node_size, tree_node_size / 8);
    uint64_t int_tree_size_required = (total_memory_size + tree_node_size) /
        (aes_block_size / 2 + tree_node_size);
    uint32_t int_tree_height = std::ceil(std::log2(int_tree_size_required));
    uint64_t int_tree_size = (uint32_t)std::exp2(int_tree_height);
    DPRINTF(CryptoCtrl,
        "\t\t\t%d integrity tree size (%d MiB, %d height, %d nodes)\n",
        int_tree_size, (float)int_tree_size / 8.f / 1024.f / 1024.f,
        int_tree_height, int_tree_size / tree_node_size);
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

bool
CryptoCtrl::CPUSidePort::sendPacket(PacketPtr pkt)
{
    owner->stats.cpuTotalCountSend++;

    bool success = sendTimingResp(pkt);
    if (!success) {
        failedPackets.push(pkt);
        ++owner->stats.cpuFailuresCountSend;
    }
    DPRINTF(CryptoCtrl, "sendPacket %s, success=%d\n", formattedPacket(pkt),
        success);

    return success;
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
    DPRINTF(CryptoCtrl, "recvTimingReq %s\n", formattedPacket(pkt));
    owner->stats.cpuTotalCountRecv++;
    return owner->handleRequest(pkt);
}

void
CryptoCtrl::CPUSidePort::recvRespRetry()
{
    bool success = true;
    // use for loop to only process packets currently in queue,
    // ignorning newly added failed packets
    for (int i = 0; i < failedPackets.size(); i++) {
        owner->stats.cpuRetryCountSend++;
        // grab next packet
        auto pkt = failedPackets.front();
        failedPackets.pop();
        // try to send packet
        success = sendTimingResp(pkt);
        DPRINTF(CryptoCtrl, "recvReqRetry %s, success=%d\n",
            formattedPacket(pkt), success);
        // keep packets which were not successfully resent
        if (!success)
            failedPackets.push(pkt);
    }
    DPRINTF(CryptoCtrl, "recvReqRetry %d failed packets in queue\n",
        failedPackets.size());
}

bool
CryptoCtrl::MemSidePort::sendPacket(PacketPtr pkt)
{
    owner->stats.memTotalCountSend++;

    // make sure we cannot miss packets
    // don't even attempt a timing req if the failure queue is not empty
    bool success = failedPackets.empty() && sendTimingReq(pkt);
    if (!success) {
        failedPackets.push(pkt);
        ++owner->stats.memFailuresCountSend;
    }
    DPRINTF(CryptoCtrl, "sendPacket %s, success=%d\n", formattedPacket(pkt),
        success);

    return success;
}

bool
CryptoCtrl::MemSidePort::recvTimingResp(PacketPtr pkt)
{
    DPRINTF(CryptoCtrl, "recvTimingResp %s\n", formattedPacket(pkt));
    owner->stats.memTotalCountRecv++;

    // just forward
    return owner->handleResponse(pkt);
}

void
CryptoCtrl::MemSidePort::recvReqRetry()
{
    bool success = true;
    // use for loop to only process packets currently in queue,
    // ignorning newly added failed packets
    //for (int i = 0; i < failedPackets.size(); i++) {
    while (success && !failedPackets.empty()) {
        owner->stats.memRetryCountSend++;
        // grab next packet
        auto pkt = failedPackets.front();
        failedPackets.pop();
        // try to send packet
        success = sendTimingReq(pkt);
        DPRINTF(CryptoCtrl, "recvReqRetry %s, success=%d\n",
            formattedPacket(pkt), success);
        // keep packets which were not successfully resent
        if (!success)
            failedPackets.push(pkt);
    }
    DPRINTF(CryptoCtrl, "recvReqRetry %d failed packets in queue\n",
        failedPackets.size());
}

void
CryptoCtrl::MemSidePort::recvRangeChange()
{
    owner->sendRangeChange();
}

bool
CryptoCtrl::handleRequest(PacketPtr pkt, bool encrypt)
{
    DPRINTF(CryptoCtrl, "handleRequest %s\n", formattedPacket(pkt));
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

    if (encrypt) {
        // consider when the incoming request becomes servicable
        // if the unit might be busy
        Tick start = curTick() > aes_enc_ready ? curTick() : aes_enc_ready;
        int iterations = pkt->getSize() / aes_block_bytes;
        // every II we schedule an iteration, considering ramp-up and ramp-down
        Tick delay = iterations * aes_enc_ii + (aes_enc_cycles - aes_enc_ii);
        Tick end = start + delay;
        // consider how long the current operations blocks other incoming
        // requests
        aes_enc_ready = start + (iterations - 1) * aes_enc_ii;

        schedule(new CryptoWriteEvent(this, pkt), end);
    } else {
        schedule(new CryptoWriteEvent(this, pkt), curTick());
    }

    return true;
}

void
CryptoCtrl::CryptoWrite(PacketPtr pkt)
{
    // for now ignore return value
    memPort.sendPacket(pkt);
}

bool
CryptoCtrl::handleResponse(PacketPtr pkt, bool decrypt)
{
    DPRINTF(CryptoCtrl, "handleResponse %s\n", formattedPacket(pkt));

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

    if (decrypt) {
        // consider when the incoming request becomes servicable,
        // if the unit might be busy
        Tick start = curTick() > aes_dec_ready ? curTick() : aes_dec_ready;
        int iterations = pkt->getSize() / aes_block_bytes;
        // every II we schedule an iteration, considering ramp-up and ramp-down
        Tick delay = iterations * aes_dec_ii + (aes_dec_cycles - aes_dec_ii);
        Tick end = start + delay;
        // consider how long the current operations blocks other incoming
        // requests
        aes_dec_ready = start + (iterations - 1) * aes_dec_ii;

        //schedule(new AESDecryptEvent(this, pkt), clockEdge(delay));
        schedule(new CryptoReadEvent(this, pkt), end);
    } else {
        schedule(new CryptoReadEvent(this, pkt), curTick());
    }

    return true;
}

void
CryptoCtrl::CryptoRead(PacketPtr pkt)
{
    // for now ignore return value
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

} // namespace gem5
