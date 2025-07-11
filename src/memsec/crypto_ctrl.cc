#include "memsec/crypto_ctrl.hh"

#include <cmath>
#include <cstdint>

#include "base/trace.hh"
#include "base/types.hh"
#include "crypto_event.hh"
#include "debug/CryptoCtrl.hh"
#include "mem/packet.hh"
#include "mem/request.hh"
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
    ss << ", size=" << unsigned(pkt->getSize());
    ss << ", read=" << unsigned(pkt->isRead());
    ss << ", reqid=" << unsigned(pkt->requestorId());
    ss << ", flags=0x" << std::hex << unsigned(pkt->req->getFlags());
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
      aes_block_bytes(params.aes_block_bits / 8),
      aes_enc_cycles(params.aes_enc_cycles),
      aes_dec_cycles(params.aes_dec_cycles), aes_enc_ii(params.aes_enc_ii),
      aes_dec_ii(params.aes_dec_ii), counter_bits(params.counter_bits),
      counter_bytes(params.counter_bits / 8), mac_bits(params.mac_bits),
      mac_bytes(params.mac_bits / 8), packing_factor(params.packing_factor),
      tree_height(params.tree_height),
      tree_node_bytes(params.tree_node_bytes),
      total_memory_addresses(params.total_memory_addresses),
      bus_bytes(params.bus_bytes), range_total(params.range_total),
      range_data(params.range_data), range_integrity(params.range_integrity),
      range_leaves(params.range_leaves), stats(this),
      mdcachePort(params.name + ".mdcache_side_port", this),
      cpuPort(params.name + ".cpu_side_port", this),
      memPort(params.name + ".mem_side_port", this)
{
    uint64_t total_memory_bytes =
        params.total_memory_addresses * params.bus_bytes;
    DPRINTF(CryptoCtrl, "Created crypto controller with properties\n");
    DPRINTF(CryptoCtrl, "\t\t\t%d total memory bytes (%f MiB)\n",
        total_memory_bytes, (double)total_memory_bytes / 1024.f / 1024.f);
    DPRINTF(CryptoCtrl, "\t\t\t%d aes block bits (%d bytes)\n",
        aes_block_bytes * 8, aes_block_bytes);
    DPRINTF(CryptoCtrl, "\t\t\t%d aes encryption cycles (%d ii)\n",
        aes_enc_cycles, aes_enc_ii);
    DPRINTF(CryptoCtrl, "\t\t\t%d aes decryption cycles (%d ii)\n",
        aes_dec_cycles, aes_dec_ii);
    DPRINTF(CryptoCtrl, "\t\t\t%d counter bits (%d bytes)\n", counter_bits,
        counter_bytes);
    DPRINTF(CryptoCtrl, "\t\t\t%d mac bits (%d bytes)\n", mac_bits, mac_bytes);
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
            (range_data.size() + range_integrity.size()) / params.bus_bytes);
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
        // immediately forward packet to mem port
        // (reads don't require processing)
        memPort.sendPacket(pkt);
    } else {
        // keep track of writes
        stats.writes++;

        // this would be the correct time to manipulate a packer during a write
        // AESEncrypt(pkt);
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

        // schedule future event, when the crypto unit finished encrypting
        schedule(new CryptoWriteEvent(this, pkt), end);

        // kick off read events for integrity tree updates
        const uint64_t non_leaf_nodes =
            (std::pow(packing_factor, tree_height) - 1) / (packing_factor - 1);
        uint64_t node_id =
            non_leaf_nodes + (pkt->getAddr() / bus_bytes) / packing_factor;
        for (int i = 0; i < tree_height - 1; i++) {
            Addr node = range_integrity.start() +
                ((node_id / (uint64_t)std::pow(packing_factor, i)) &
                    (-1 - (tree_node_bytes - 1)));
            DPRINTF(CryptoCtrl,
                "translating address 0x%x into integrity tree address 0x%x "
                "at tree level %d\n",
                pkt->getAddr(), node, i);
            panic_if(!range_integrity.contains(node),
                "the node address 0x%x must lie within the integrity memory "
                "range %s\n",
                node, range_integrity.to_string());

            PacketPtr packet_fetch_md =
                createPkt(node, tree_node_bytes, 0, 0, MemCmd::ReadReq);
            DPRINTF(CryptoCtrl, "launching request %d packet %s\n", i,
                formattedPacket(packet_fetch_md));
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
}

bool
CryptoCtrl::handleResponse(PacketPtr pkt, ResponseSource source)
{
    DPRINTF(CryptoCtrl, "handleResponse %s, type=%s\n", formattedPacket(pkt),
        source);

    switch (source) {
    case ResponseSource::MemoryController:
        if (pkt->isRead()) {
            // consider when the incoming request becomes servicable,
            // if the unit might be busy
            Tick start =
                curTick() > aes_dec_ready ? curTick() : aes_dec_ready;
            int iterations = pkt->getSize() / aes_block_bytes;
            // every II we schedule an iteration, considering ramp-up and
            // ramp-down
            Tick delay =
                iterations * aes_dec_ii + (aes_dec_cycles - aes_dec_ii);
            Tick end = start + delay;
            // consider how long the current operations blocks other incoming
            // requests
            aes_dec_ready = start + (iterations - 1) * aes_dec_ii;

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
        } else {
            // directly forward the data
            cpuPort.sendPacket(pkt);
        }
        break;
    case ResponseSource::MetadataCache:
        DPRINTF(CryptoCtrl, "unimplemented\n");
        break;
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
    // return the mem port ranges
    return memPort.getAddrRanges();
}

void
CryptoCtrl::sendRangeChange()
{
    cpuPort.sendRangeChange();
}

} // namespace gem5
