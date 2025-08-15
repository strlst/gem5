#include "memsec/crypto_ctrl.hh"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "base/trace.hh"
#include "base/types.hh"
#include "crypto_ctrl.hh"
#include "debug/CryptoCtrl.hh"
#include "mem/packet.hh"
#include "mem/request.hh"
#include "memsec/crypto_event.hh"
#include "sim/clocked_object.hh"
#include "sim/system.hh"

namespace gem5
{

inline std::string
formattedPacket(PacketPtr pkt)
{
    std::ostringstream ss;
    ss << "pkt(";
    ss << "addr=0x" << std::hex << pkt->getAddr() << std::dec;
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

inline PacketPtr
CryptoCtrl::createPkt(Addr addr, size_t size, MemCmd cmd)
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
CryptoCtrl::createPktFromPkt(PacketPtr pkt, MemCmd cmd)
{
    PacketPtr new_pkt = createPkt(pkt->getAddr(), pkt->getSize(), cmd);
    memcpy(new_pkt->getPtr<uint8_t>(), pkt->getPtr<uint8_t>(), pkt->getSize());
    return new_pkt;
}

CryptoCtrl::CryptoCtrl(const CryptoCtrlParams& params)
    : ClockedObject(params), sys(params.system),
      requestorId(sys->getRequestorId(this)), aes_enc_ready(0),
      aes_dec_ready(0), mac_ready(0),
      aes_block_bytes(params.aes_block_bits / 8),
      counter_bytes(params.counter_bits / 8), mac_bytes(params.mac_bits / 8),
      aes_enc_cycles(params.aes_enc_cycles),
      aes_dec_cycles(params.aes_dec_cycles), aes_enc_ii(params.aes_enc_ii),
      aes_dec_ii(params.aes_dec_ii), mac_cycles(params.mac_cycles),
      mac_ii(params.mac_ii), packing_factor(params.packing_factor),
      tree_height(params.tree_height),
      tree_node_bytes(params.tree_node_bytes), bus_bytes(params.bus_bytes),
      range_total(params.range_total), range_data(params.range_data),
      range_integrity(params.range_integrity),
      range_leaves(params.range_leaves), int_trb(params.int_trb), stats(this),
      mdcachePort(params.name + ".mdcache_side_port", this),
      cpuPort(params.name + ".cpu_side_port", this),
      memPort(params.name + ".mem_side_port", this)
{
    DPRINTF(CryptoCtrl, "Created crypto controller with properties\n");
    DPRINTF(CryptoCtrl, "\t\t\t%d total memory bytes (%f MiB)\n",
        params.range_total.size(),
        (double)params.range_total.size() / 1024.f / 1024.f);
    DPRINTF(CryptoCtrl, "\t\t\t%d aes block bits (%d bytes)\n",
        aes_block_bits, aes_block_bytes);
    DPRINTF(CryptoCtrl, "\t\t\t%d aes encryption cycles (%d ii)\n",
        aes_enc_cycles, aes_enc_ii);
    DPRINTF(CryptoCtrl, "\t\t\t%d aes decryption cycles (%d ii)\n",
        aes_dec_cycles, aes_dec_ii);
    DPRINTF(CryptoCtrl, "\t\t\t%d counter bits (%d bytes)\n", counter_bits,
        counter_bytes);
    DPRINTF(CryptoCtrl, "\t\t\t%d mac bits (%d bytes)\n", mac_bits, mac_bytes);
    DPRINTF(CryptoCtrl, "\t\t\t%d mac cycles (%d ii)\n", mac_cycles, mac_ii);
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
    } else if (if_name == "metadata_cache_side_port") {
        return mdcachePort;
    } else {
        // pass it along to our super class
        return SimObject::getPort(if_name, idx);
    }
}

bool
CryptoCtrl::MetadataCacheSidePort::sendPacket(PacketPtr pkt)
{
    owner->stats.mdcacheTotalCountSend++;

    // make sure we cannot miss packets
    // don't even attempt a timing req if the failure queue is not empty
    bool success = failedPackets.empty() && sendTimingReq(pkt);
    if (!success) {
        failedPackets.push(pkt);
        ++owner->stats.mdcacheFailuresCountSend;
    }
    DPRINTF(CryptoCtrl, "sendPacket %s, success=%d\n", formattedPacket(pkt),
        success);

    return success;
}

bool
CryptoCtrl::MetadataCacheSidePort::recvTimingResp(PacketPtr pkt)
{
    DPRINTF(CryptoCtrl, "recvTimingResp %s\n", formattedPacket(pkt));
    owner->stats.mdcacheTotalCountRecv++;

    // just forward
    return owner->handleResponse(pkt, ResponseSource::MetadataCache);
}

void
CryptoCtrl::MetadataCacheSidePort::recvReqRetry()
{
    bool success = true;
    // use for loop to only process packets currently in queue,
    // ignorning newly added failed packets
    //for (int i = 0; i < failedPackets.size(); i++) {
    while (success && !failedPackets.empty()) {
        owner->stats.mdcacheRetryCountSend++;
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
CryptoCtrl::MetadataCacheSidePort::recvRangeChange()
{
    owner->sendRangeChange();
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
    return owner->handleResponse(pkt, ResponseSource::MemoryController);
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
    std::pair<bool, IntTreeReq> response =
        int_trb->enqueue_request(pkt->getAddr(), pkt->isRead());
    if (!response.first) {
        int_tree_retry_necessary = true;
        DPRINTF(CryptoCtrl,
            "busy due to full buffer or existing request, postponing %s\n",
            formattedPacket(pkt));
        return false;
    }

    if (pkt->isRead()) {
        // immediately forward packet to mem port
        // (reads don't require processing)
        memPort.sendPacket(pkt);

        // reads for integrity verification
        for (auto& node : response.second.nodes) {
            // create and send packets for each layer
            PacketPtr packet_fetch_md =
                createPkt(node.address, tree_node_bytes, MemCmd::ReadReq);
            mdcachePort.sendPacket(packet_fetch_md);
        }
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

        // reads for integrity verification
        for (auto& node : response.second.nodes) {
            // create and send packets for each layer
            PacketPtr packet_fetch_md =
                createPkt(node.address, tree_node_bytes, MemCmd::ReadReq);
            mdcachePort.sendPacket(packet_fetch_md);
        }
    }

    return true;
}

void
CryptoCtrl::CryptoWrite(PacketPtr pkt)
{
    // for now ignore return value
    memPort.sendPacket(pkt);
    scheduleMACOp(pkt, MACEventType::DataMACUpdate);
}

bool
CryptoCtrl::handleResponse(PacketPtr pkt, ResponseSource source)
{
    DPRINTF(CryptoCtrl, "handleResponse %s, type=%s\n", formattedPacket(pkt),
        source);

    switch (source) {
    case ResponseSource::MemoryController:
        // this should not occur, but short circuit responses which should not
        // be routed to the CPU
        if (range_integrity.contains(pkt->getAddr())) {
            return true;
        }

        if (pkt->isRead()) {
            scheduleAESDecryptOp(pkt);
            scheduleMACOp(pkt, MACEventType::DataMACCheck);
        } else {
            // directly forward the data
            cpuPort.sendPacket(pkt);
        }
        break;
    case ResponseSource::MetadataCache:
        if (pkt->isRead()) {
            // depending on whether we are just checking the integrity tree,
            // or reading to update the integrity tree metadata, we have to
            // react differently
            if (int_trb->contains_request_node(pkt->getAddr(), true)) {
                // the node we are handling is contained in a read request
                scheduleMACOp(pkt, MACEventType::IntegrityMACCheck);
            } else {
                // the node we are handling is contained in a write request
                // the cache has yielded the data, now we update the counters
                // and hash
                PacketPtr new_pkt = createPktFromPkt(pkt, MemCmd::WriteReq);
                DPRINTF(CryptoCtrl,
                    "created write-after-read request packet %s\n",
                    formattedPacket(new_pkt));
                int_trb->update_metadata(new_pkt);
                mdcachePort.sendPacket(new_pkt);
            }
        } else {
            // forward completion event to queue
            bool completed = int_trb->complete_request_node(pkt->getAddr());
            DPRINTF(CryptoCtrl, "\n");
            if (completed) {
                scheduleMACOp(pkt, MACEventType::IntegrityMACUpdate);
            }
        }
        break;
    }

    return true;
}

void
CryptoCtrl::CryptoRead(PacketPtr pkt)
{
    // for now ignore return value
    cpuPort.sendPacket(pkt);
    // also check the data MAC itself for integrity
    scheduleMACOp(pkt, MACEventType::DataMACCheck);
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
CryptoCtrl::IntegrityMACCheck(PacketPtr pkt)
{
    DPRINTF(CryptoCtrl, "finished integrity MAC check %s\n",
        formattedPacket(pkt));
    // TODO: consider adding extra logic to gate completion of events to
    // times where their parent event reads have already finished
    bool completed = int_trb->complete_request_node(pkt->getAddr());
    if (completed) {
        int_trb->release_request(pkt->getAddr());
        if (int_tree_retry_necessary) {
            int_tree_retry_necessary = false;
            // try to retry failed requests at this point
            DPRINTF(CryptoCtrl, "retry from tree update queue\n");
            cpuPort.sendRetryReq();
        }
    }
}

void
CryptoCtrl::IntegrityMACUpdate(PacketPtr pkt)
{
    DPRINTF(CryptoCtrl, "finished integrity MAC update %s\n",
        formattedPacket(pkt));
    // theoretically we would update the MAC here
    int_trb->release_request(pkt->getAddr());
    if (int_tree_retry_necessary) {
        int_tree_retry_necessary = false;
        // try to retry failed requests at this point
        DPRINTF(CryptoCtrl, "retry from tree update queue\n");
        cpuPort.sendRetryReq();
    }
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

    // this would be the correct time to manipulate a packer during a write
    // AESEncrypt(pkt);
    // schedule future event, when the crypto unit finished encrypting
    schedule(new CryptoWriteEvent(this, pkt), end);
}

void
CryptoCtrl::scheduleAESDecryptOp(PacketPtr pkt)
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
    schedule(new CryptoReadEvent(this, pkt), end);
}

void
CryptoCtrl::scheduleMACOp(PacketPtr pkt, MACEventType type)
{
    Tick start = curTick() > mac_ready ? curTick() : mac_ready;
    Tick end = start + CYCLES_TO_TICKS(mac_cycles);
    // consider how long the current operations blocks other
    // incoming requests
    mac_ready = start + CYCLES_TO_TICKS(mac_ii);
    schedule(new MACEvent(this, pkt, type), end);
}

} // namespace gem5
