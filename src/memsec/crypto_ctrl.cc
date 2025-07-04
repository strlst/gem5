#include "memsec/crypto_ctrl.hh"

#include <cmath>
#include <cstdint>
#include <queue>

#include "base/trace.hh"
#include "base/types.hh"
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
      aes_block_bits(params.aes_block_bits),
      aes_block_bytes(params.aes_block_bits / 8),
      aes_enc_cycles(params.aes_enc_cycles),
      aes_dec_cycles(params.aes_dec_cycles), aes_enc_ii(params.aes_enc_ii),
      aes_dec_ii(params.aes_dec_ii), counter_bits(params.counter_bits),
      counter_bytes(params.counter_bits / 8), mac_bits(params.mac_bits),
      mac_bytes(params.mac_bits / 8), packing_factor(params.packing_factor),
      bytes_per_address(params.bytes_per_address), stats(this),
      cpuPort(params.name + ".cpu_side_port", this),
      memPort(params.name + ".mem_side_port", this)
{
    // make some calculations for automatic integrity tree creation
    // not all fields are really needed, but for convenience they remain
    total_memory_addresses = params.range.size();
    total_memory_blocks =
        total_memory_addresses * bytes_per_address / aes_block_bytes;
    total_memory_bytes = total_memory_addresses * bytes_per_address;
    total_memory_bits = total_memory_bytes * 8;
    tree_node_bits = counter_bits * packing_factor + mac_bits;
    tree_node_bytes = tree_node_bits / 8;

    // calculate integrity tree parameters
    // this formula is derived by hand
    uint64_t int_tree_height =
        (uint64_t)(std::ceil(
            std::log2((total_memory_addresses * (packing_factor - 1) +
                          tree_node_bytes) /
                (aes_block_bytes * (packing_factor - 1) + tree_node_bytes)) /
            std::log2(packing_factor))) -
        1;

    // calculate region sizes in bytes
    tree_node_count =
        std::pow(packing_factor, int_tree_height + 1) / (packing_factor - 1);
    leaf_node_count = std::pow(packing_factor, int_tree_height);
    region_integrity_bytes = (tree_node_count - 1) * tree_node_bytes;
    region_data_bytes = total_memory_bytes - region_integrity_bytes;

    // assign memory regions
    region_data = AddrRange(params.range.start(),
        params.range.start() + region_data_bytes / bytes_per_address);
    region_integrity = AddrRange(region_data.end(),
        region_data.end() + region_integrity_bytes / bytes_per_address);

    integrity_tree.reset(
        new FlatTree<BMTNode>(int_tree_height, packing_factor));
    DPRINTF(CryptoCtrl, "updated %d elements\n", integrity_tree->get_size());

    DPRINTF(CryptoCtrl, "Created crypto controller with properties\n");
    DPRINTF(CryptoCtrl, "\t\t\t%d total memory bytes (%f MiB)\n",
        total_memory_bytes, (double)total_memory_bytes / 1024.f / 1024.f);
    DPRINTF(CryptoCtrl, "\t\t\t%d aes block bits (%d bytes)\n",
        aes_block_bits, aes_block_bytes);
    DPRINTF(CryptoCtrl, "\t\t\t%d aes encryption cycles (%d ii)\n",
        aes_enc_cycles, aes_enc_ii);
    DPRINTF(CryptoCtrl, "\t\t\t%d aes decryption cycles (%d ii)\n",
        aes_dec_cycles, aes_dec_ii);
    DPRINTF(CryptoCtrl, "\t\t\t%d counter bits (%d bytes)\n", counter_bits,
        counter_bytes);
    DPRINTF(CryptoCtrl, "\t\t\t%d mac bits (%d bytes)\n", mac_bits, mac_bytes);
    DPRINTF(CryptoCtrl,
        "\t\t\t%d integrity tree node bits (%d bytes, %d packing factor)\n",
        tree_node_bits, tree_node_bytes, packing_factor);
    DPRINTF(CryptoCtrl, "\t\t\t%d integrity tree bytes (%f MiB, %d height)\n",
        region_integrity_bytes,
        (double)region_integrity_bytes / 1024.f / 1024.f, int_tree_height);
    DPRINTF(CryptoCtrl,
        "\t\t\t%d integrity tree nodes (%d leaves, 0x%x max addr)\n",
        tree_node_count,
        leaf_node_count,
        integrity_tree->get_max_address());
    DPRINTF(CryptoCtrl, "\t\t\t%s memory region (total)\n",
        params.range.to_string());
    DPRINTF(CryptoCtrl,
        "\t\t\t%s memory region (data, %d address bits required)\n",
        region_data.to_string(),
        std::log2(region_data.end() - region_data.start()));
    DPRINTF(CryptoCtrl,
        "\t\t\t%s memory region (integrity, %d address bits required)\n",
        region_integrity.to_string(),
        std::log2(region_integrity.end() - region_integrity.start()));
    DPRINTF(CryptoCtrl,
        "\t\t\t[0x%x:0x%x] actual region (integrity, %d address bits "
        "required)\n",
        region_integrity.start(),
        region_integrity.start() + integrity_tree->get_max_address() - 1,
        std::log2(integrity_tree->get_max_address()));

    // sanity check
    assert(region_data_bytes + region_integrity_bytes <= total_memory_bytes);
    assert(region_data_bytes > region_integrity_bytes);
    assert(region_data.start() < region_data.end());
    assert(region_data.end() == region_integrity.start());
    assert(region_integrity.start() < region_integrity.end());
    assert(params.range.end() >= params.range.start() +
            (region_data_bytes + region_integrity_bytes) / bytes_per_address);
    assert(params.range.start() == region_data.start());
    assert(params.range.end() >= region_integrity.end());
    assert(region_integrity.size() < region_data.size());
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

    // next we save the new counter and
    // update the integrity tree along the path
    // NOTE: assume addresses are byte addresses and already aligned
    // on DRAM bus width
    // to get a leaf node address from a data address, we need to convert
    // appropriately
    // leaf i = address / packing_factor, counter c = address % packing_factor
    uint64_t node_index =
        (pkt->getAddr() / bytes_per_address) / packing_factor;
    uint64_t offset = (pkt->getAddr() / bytes_per_address) % packing_factor;
    // we need to offset the node index to the region where the leaves
    // are stored
    Addr node_address = node_index + tree_node_count - leaf_node_count;
    Addr parent_address;

    // TODO: implement accurate timing of this operation and schedule
    // memory operations
    DPRINTF(CryptoCtrl,
        "write on data block 0x%x stored at node %d, offset %d\n",
        pkt->getAddr(), node_index, offset);

    // recalculate integrity values up the tree
    do {
        auto& node = integrity_tree->lookup(node_address);
        parent_address = integrity_tree->parent_address(node_address);
        offset = integrity_tree->child_offset(node_address);
        auto& parent = integrity_tree->lookup(parent_address);
        node.increment(offset);
        parent.increment(offset);
        node.update_mac(parent.counters.at(parent_address % packing_factor));
        DPRINTF(CryptoCtrl,
            "tree update @ address 0x%x, offset 0x%x, parent address 0x%x, "
            "%s\n",
            node_address, offset, parent_address, node.to_string());
        node_address = parent_address;
    } while (node_address != 0);
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
    // return the mem port ranges
    return memPort.getAddrRanges();
}

void
CryptoCtrl::sendRangeChange()
{
    cpuPort.sendRangeChange();
}

} // namespace gem5
