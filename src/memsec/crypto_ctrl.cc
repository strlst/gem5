#include "memsec/crypto_ctrl.hh"

#include <cmath>
#include <cstring>

#include "base/logging.hh"
#include "base/trace.hh"
#include "base/types.hh"
#include "crypto_ctrl.hh"
#include "debug/CryptoCtrl.hh"
#include "mem/packet.hh"
#include "mem/request.hh"
#include "memsec/aes_unit.hh"
#include "memsec/crypto_event.hh"
#include "memsec/util.hh"
#include "sim/clocked_object.hh"
#include "sim/system.hh"

namespace gem5
{

CryptoCtrl::CryptoCtrl(const CryptoCtrlParams& params)
    : ClockedObject(params), sys(params.system),
      requestorId(sys->getRequestorId(this)),
      counter_bytes(params.counter_bits / 8),
      packing_factor(params.packing_factor), tree_height(params.tree_height),
      tree_node_bytes(params.tree_node_bytes), bus_bytes(params.bus_bytes),
      range_total(params.range_total), range_data(params.range_data),
      range_integrity(params.range_integrity),
      range_leaves(params.range_leaves), aes_unit(params.aes_unit),
      mac_unit(params.mac_unit), int_trb(params.int_trb), stats(this),
      cpuPort(params.name + ".cpu_side_port", this),
      memPort(params.name + ".mem_side_port", this)
{
    DPRINTF(CryptoCtrl, "Created crypto controller with properties\n");
    DPRINTF(CryptoCtrl, "\t\t\t%d total memory bytes (%f MiB)\n",
        params.range_total.size(),
        (double)params.range_total.size() / 1024.f / 1024.f);
    DPRINTF(CryptoCtrl, "\t\t\t%d counter bits (%d bytes)\n", counter_bits,
        counter_bytes);
    DPRINTF(CryptoCtrl,
        "\t\t\t%d integrity tree node bits (%d bytes, %d packing factor, %d "
        "height)\n",
        tree_node_bytes * 8, tree_node_bytes, packing_factor, tree_height);
    DPRINTF(CryptoCtrl,
        "\t\t\t%s memory region (total, %d address bits required)\n",
        range_total.to_string(),
        std::log2(range_total.end() - range_total.start()));
    DPRINTF(CryptoCtrl,
        "\t\t\t%s memory region (data, %d address bits required)\n",
        range_data.to_string(),
        std::log2(range_data.end() - range_data.start()));
    DPRINTF(CryptoCtrl,
        "\t\t\t%s memory region (integrity, %d address bits required)\n",
        range_integrity.to_string(),
        std::log2(range_integrity.end() - range_integrity.start()));
    DPRINTF(CryptoCtrl,
        "\t\t\t%s memory region (leaves, %d address bits "
        "required)\n",
        range_leaves.to_string(),
        std::log2(range_leaves.end() - range_leaves.start()));

    // sanity check
    assert(range_data.size() + range_integrity.size() <= range_total.size());
    assert(range_data.size() > range_integrity.size());
    assert(range_data.start() < range_data.end());
    assert(range_data.end() == range_integrity.start());
    assert(range_integrity.start() < range_integrity.end());
    assert(range_leaves.start() < range_leaves.end());
    assert(params.range_total.end() >= params.range_total.start() +
            (range_data.size() + range_integrity.size()));
    assert(params.range_total.start() == range_data.start());
    assert(params.range_total.end() >= range_integrity.end());
    assert(range_integrity.size() < range_data.size());
    assert(range_leaves.size() < range_integrity.size());
}

Port&
CryptoCtrl::getPort(const std::string& if_name, PortID idx)
{
    panic_if(idx != InvalidPortID, "This object doesn't support vector ports");

    // this is the name from the Python SimObject declaration (CryptoCtrl.py)
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
CryptoCtrl::handleRequest(PacketPtr pkt)
{
    DPRINTF(CryptoCtrl, "handleRequest %s\n", formattedPacket(pkt));
    if (pkt->isRead()) {
        // keep track of reads
        stats.reads++;
    } else {
        // keep track of writes
        stats.writes++;
    }

    panic_if(range_integrity.contains(pkt->getAddr()),
        "requests to CryptoCtrl are not allowed to fall into the reserved "
        "integrity region!\n");

    // enqueue integrity tree request in buffer

    panic_if(int_trb->is_full(),
        "currently queue is not allowed to be full\n");
    int_trb->enqueue_request(pkt->getAddr(), pkt->isRead());

    if (pkt->isRead()) {
        // immediately forward packet to mem port
        // (reads don't require processing)
        memPort.sendPacket(pkt);
    } else {
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

        // decryption
        scheduleAESEncryptOp(pkt);
    }

    return true;
}

void
CryptoCtrl::CryptoWrite(PacketPtr pkt)
{
    // for now ignore return value
    // TODO: should we block sendpacket until MAC unit is available?
    bool success = memPort.sendPacket(pkt);
    panic_if(!success, "mem port send packet is not allowed to fail\n");
    scheduleMACOp(pkt, DataMACEventType::DataMACUpdate);
}

bool
CryptoCtrl::handleResponse(PacketPtr pkt)
{
    DPRINTF(CryptoCtrl, "handleResponse %s, type=%s\n", formattedPacket(pkt));

    // NOTE: integrity side of things is decoupled

    // this should not occur, but short circuit responses which should not
    // be routed to the CPU
    if (range_integrity.contains(pkt->getAddr())) {
        return true;
    }

    if (pkt->isRead()) {
        scheduleAESDecryptOp(pkt);
        scheduleMACOp(pkt, DataMACEventType::DataMACCheck);
    } else {
        // directly forward the data
        cpuPort.sendPacket(pkt);
    }

    return true;
}

void
CryptoCtrl::CryptoRead(PacketPtr pkt)
{
    // TODO: should we block sendpacket until MAC unit is available?
    // for now ignore return value
    bool success = cpuPort.sendPacket(pkt);
    panic_if(!success, "mem port send packet is not allowed to fail\n");
    // also check the data MAC itself for integrity
    scheduleMACOp(pkt, DataMACEventType::DataMACCheck);
}

void
CryptoCtrl::DataMACCheck(PacketPtr pkt)
{
    DPRINTF(CryptoCtrl, "finished data MAC check %s\n", formattedPacket(pkt));
    // theoretically we would check the MAC here
}

void
CryptoCtrl::DataMACUpdate(PacketPtr pkt)
{
    DPRINTF(CryptoCtrl, "finished data MAC update %s\n", formattedPacket(pkt));
    // theoretically we would update the MAC here
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
    // return the mem port ranges
    return memPort.getAddrRanges();
}

void
CryptoCtrl::sendRangeChange()
{
    cpuPort.sendRangeChange();
}

void
CryptoCtrl::scheduleAESEncryptOp(PacketPtr pkt)
{
    // this would be the correct time to manipulate a packer during a write
    // AESEncrypt(pkt);
    // schedule future event, when the crypto unit finished encrypting
    schedule(new CryptoWriteEvent(this, pkt),
        aes_unit->get_earliest_enc_ready_time(pkt));
}

void
CryptoCtrl::scheduleAESDecryptOp(PacketPtr pkt)
{
    // if data should be processed, now would be the time to process it
    // AESDecrypt(pkt);
    /*
        // print data
        uint8_t* byte = pkt->getPtr<uint8_t>();
        for (int i = 0; i < pkt->getSize(); i++) {
            printf("%.02x", *(byte++));
        }
        printf("\n");
    */

    //schedule(new AESDecryptEvent(this, pkt), clockEdge(delay));
    schedule(new CryptoReadEvent(this, pkt),
        aes_unit->get_earliest_dec_ready_time(pkt));
}

void
CryptoCtrl::scheduleMACOp(PacketPtr pkt, DataMACEventType type)
{
    schedule(new DataMACEvent(this, pkt, type),
        mac_unit->get_earliest_ready_time(pkt));
}

} // namespace gem5
