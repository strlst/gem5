#include "memsec/aim_ctrl.hh"

#include <cmath>
#include <cstring>

#include "base/logging.hh"
#include "base/trace.hh"
#include "base/types.hh"
#include "debug/AIMCtrl.hh"
#include "mem/packet.hh"
#include "mem/request.hh"
#include "memsec/aes_unit.hh"
#include "memsec/aim_event.hh"
#include "memsec/util.hh"
#include "sim/clocked_object.hh"
#include "sim/system.hh"

using namespace std::placeholders;

namespace gem5
{

AIMCtrl::AIMCtrl(const AIMCtrlParams& params)
    : ClockedObject(params), sys(params.system),
      requestorId(sys->getRequestorId(this)),
      counter_bytes(params.counter_bits / 8),
      packing_factor(params.packing_factor), tree_height(params.tree_height),
      tree_node_bytes(params.tree_node_bytes), bus_bytes(params.bus_bytes),
      range_total(params.range_total), range_data(params.range_data),
      range_integrity(params.range_integrity),
      range_leaves(params.range_leaves), aes_unit(params.aes_unit),
      mac_unit(params.mac_unit), int_trb(params.int_trb),
      tickEvent(
          [this] {
              tick();
          }, name()),
      stats(this), cpuPort(params.name + ".cpu_side_port", this),
      memPort(params.name + ".mem_side_port", this)
{
    DPRINTF(AIMCtrl, "Created crypto controller with properties\n");
    DPRINTF(AIMCtrl, "\t\t\t%d total memory bytes (%f MiB)\n",
        params.range_total.size(),
        (double)params.range_total.size() / 1024.f / 1024.f);
    DPRINTF(AIMCtrl, "\t\t\t%d counter bits (%d bytes)\n", counter_bytes * 8,
        counter_bytes);
    DPRINTF(AIMCtrl,
        "\t\t\t%d integrity tree node bits (%d bytes, %d packing factor, %d "
        "height)\n",
        tree_node_bytes * 8, tree_node_bytes, packing_factor, tree_height);
    DPRINTF(AIMCtrl,
        "\t\t\t%s memory region (total, %d address bits required)\n",
        range_total.to_string(),
        std::log2(range_total.end() - range_total.start()));
    DPRINTF(AIMCtrl,
        "\t\t\t%s memory region (data, %d address bits required)\n",
        range_data.to_string(),
        std::log2(range_data.end() - range_data.start()));
    DPRINTF(AIMCtrl,
        "\t\t\t%s memory region (integrity, %d address bits required)\n",
        range_integrity.to_string(),
        std::log2(range_integrity.end() - range_integrity.start()));
    DPRINTF(AIMCtrl,
        "\t\t\t%s memory region (leaves, %d address bits "
        "required)\n",
        range_leaves.to_string(),
        std::log2(range_leaves.end() - range_leaves.start()));

    int_trb->register_counter_read_callback(
        std::bind(&AIMCtrl::onCounterRead, this, _1, _2, _3));
    int_trb->register_release_callback(
        std::bind(&AIMCtrl::onIntTRBCompletedRequest, this));
    DPRINTF(AIMCtrl, "Registered release callback for IntTRB unit\n");

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
AIMCtrl::getPort(const std::string& if_name, PortID idx)
{
    panic_if(idx != InvalidPortID, "This object doesn't support vector ports");

    // this is the name from the Python SimObject declaration (AIMCtrl.py)
    if (if_name == "mem_side_port") {
        return memPort;
    } else if (if_name == "cpu_side_port") {
        return cpuPort;
    } else {
        // pass it along to our super class
        return SimObject::getPort(if_name, idx);
    }
}

void
AIMCtrl::startup()
{
    // kick off the clock ticks
    // NOTE: don't use tick event, it costs too much performance
    //schedule(tickEvent, clockEdge());
}

void
AIMCtrl::tick()
{
    // NOTE: this method is not used currently, but left in here for anyone
    //       who might want to implement a feature using cycle-level events
    /*
    schedule(tickEvent, clockEdge(Cycles(1)));
    if (cpu_failed_packets > 0 && !int_trb->is_busy()) {
        DPRINTF(AIMCtrl, "sending cpu retry request\n");
        cpuPort.sendRetryReq();
        cpu_failed_packets--;
    }
    */
}

bool
AIMCtrl::handleRequest(PacketPtr pkt)
{
    DPRINTF(AIMCtrl, "handleRequest %s\n", formattedPacket(pkt));

    // keep track of reads and writes
    stats.totalRequests++;
    if (pkt->isRead())
        stats.totalReads++;
    else
        stats.totalWrites++;

    if (pkt->cmd == MemCmd::CleanEvict) {
        stats.successfulRequests++;
        stats.successfulWrites++;
        memPort.sendPacket(pkt);
        return true;
    }

    PacketId data_id = pkt->id;
    Addr data_address = pkt->getAddr();
    panic_if(range_integrity.contains(data_address),
        "requests to AIMCtrl are not allowed to fall into the reserved "
        "integrity region!\n");

    // block reads for queued but as of yet unscheduled writes
    // to prevent read requests from overtaking delayed write requests
    // in the memory controller buffer!
    if (auto it = blocked_set.find(data_address);
        pkt->isRead() && it != blocked_set.end()) {
        DPRINTF(AIMCtrl,
            "request for address 0x%x placed while there is an unresolved "
            "on-going write request\n",
            data_address);
        stats.refusedRequests++;
        stats.refusedReads++;
        cpu_failed_packets++;
        return false;
    }

    // first enqueue integrity tree request in buffer
    if (int_trb->is_busy()) {
        // fail on full queue
        DPRINTF(AIMCtrl, "IntTRB is full, refusing request\n");
        stats.refusedRequests++;
        if (pkt->isRead())
            stats.refusedReads++;
        else
            stats.refusedWrites++;
        cpu_failed_packets++;
        return false;
    }

    // keep track of successful operations
    stats.successfulRequests++;
    if (pkt->isRead())
        stats.successfulReads++;
    else
        stats.successfulWrites++;

    // queue definitely has space
    // get associated counter address
    int_trb->enqueue_request(data_address, data_id, pkt->isRead());

    if (pkt->isRead()) {
        // immediately forward packet to mem port
        // (reads don't require processing before going to the memctrl)
        read_queue.add(pkt, data_id);
        DPRINTF(AIMCtrl,
            "read_queue: emplaced request (data_id=%ld)\n",
            data_id);
        memPort.sendPacket(pkt);
    } else {
        /*
        // assume multiples of 64 bytes
        const uint64_t mask = 0xFFFFFFFFFFFFFFFF;
        if (pkt->hasData()) {
            DPRINTF(AIMCtrl, "XORing write data at %d\n", pkt->getAddr());
            uint64_t* data = pkt->getPtr<uint64_t>();
            for (int i = 0; i < pkt->getSize() / 8; i++) {
                *data = (*data) ^ mask;
                data++;
            }
        }
        */

        // decryption
        // synchronize with counter read AES keystream generation
        write_queue.add(pkt, data_id);
        DPRINTF(AIMCtrl,
            "write_queue: emplaced request (data_id=%ld)\n",
            data_id);
        // we need to mark address as busy so that they cannot compete with
        // concurrent read requests
        blocked_set.emplace(pkt->getAddr());
        // immediately complete data op since write data is already available
        DPRINTF(AIMCtrl, "(AIMWrite) completed data op %ld\n", pkt->id);
        bool complete = write_queue.complete_data_op(data_id);
        panic_if(complete,
            "counter read should not complete before data op in the case of "
            "writes!\n");
    }

    return true;
}

bool
AIMCtrl::handleResponse(PacketPtr pkt)
{
    DPRINTF(AIMCtrl, "handleResponse %s\n", formattedPacket(pkt));

    // NOTE: integrity side of things is decoupled

    // this should not occur, but short circuit responses which should not
    // be routed to the CPU
    if (range_integrity.contains(pkt->getAddr())) {
        return true;
    }

    if (pkt->isRead()) {
        // if queue returns *true*, that means we have already generated the
        // AES keystream for this packet, so we can just return the packet to
        // the CPU and finish this request
        DPRINTF(AIMCtrl, "(AIMRead) completed data op %ld\n", pkt->id);
        if (read_queue.complete_data_op(pkt->id)) {
            DPRINTF(AIMCtrl,
                "read_queue: completed data read event for data pkt %ld\n",
                pkt->id);
            bool success = cpuPort.sendPacket(pkt);
            panic_if(!success,
                "cpu port send packet is not allowed to fail\n");
            read_queue.erase_data_read(pkt->id);
        }
        scheduleMACOp(pkt, DataMACEventType::DataMACCheck);
    } else {
        // directly forward the data
        cpuPort.sendPacket(pkt);
    }

    return true;
}

void
AIMCtrl::handleFunctional(PacketPtr pkt)
{
    // just pass this on to the memory side to handle for now
    memPort.sendFunctional(pkt);
}

AddrRangeList
AIMCtrl::getAddrRanges() const
{
    // return the mem port ranges
    return memPort.getAddrRanges();
}

void
AIMCtrl::sendRangeChange()
{
    cpuPort.sendRangeChange();
}

void
AIMCtrl::scheduleAESEncryptOp(PacketPtr pkt, std::vector<PacketId> data_ids)
{
    // this would be the correct time to manipulate a packer during a write
    // AESEncrypt(pkt);
    // schedule future event, when the crypto unit finished encrypting
    schedule(new AIMWriteEvent(this, pkt, data_ids),
        aes_unit->get_earliest_enc_ready_time(pkt));
}

void
AIMCtrl::scheduleAESDecryptOp(PacketPtr pkt, std::vector<PacketId> data_ids)
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
    schedule(new AIMReadEvent(this, pkt, data_ids),
        aes_unit->get_earliest_dec_ready_time(pkt));
}

void
AIMCtrl::scheduleMACOp(PacketPtr pkt, DataMACEventType type)
{
    schedule(new DataMACEvent(this, pkt, type),
        mac_unit->get_earliest_ready_time(pkt));
}

AddrRangeList
AIMCtrl::CPUSidePort::getAddrRanges() const
{
    return owner->getAddrRanges();
}

bool
AIMCtrl::CPUSidePort::sendPacket(PacketPtr pkt)
{
    owner->stats.cpuTotalCountSend++;

    bool success = sendTimingResp(pkt);
    if (!success) {
        failedPackets.push(pkt);
        ++owner->stats.cpuFailuresCountSend;
    }
    DPRINTF(AIMCtrl, "sendPacket %s, success=%d\n", formattedPacket(pkt),
        success);

    return success;
}

bool
AIMCtrl::CPUSidePort::hasFailedPackets()
{
    return failedPackets.size() > 0;
}

void
AIMCtrl::CPUSidePort::recvFunctional(PacketPtr pkt)
{
    // just forward
    return owner->handleFunctional(pkt);
}

bool
AIMCtrl::CPUSidePort::recvTimingReq(PacketPtr pkt)
{
    DPRINTF(AIMCtrl, "recvTimingReq %s\n", formattedPacket(pkt));
    owner->stats.cpuTotalCountRecv++;

    // TODO: log sequence here?

    // just forward
    return owner->handleRequest(pkt);
}

void
AIMCtrl::CPUSidePort::recvRespRetry()
{
    bool success = true;
    // use for loop to only process packets currently in queue,
    // ignorning newly added failed packets
    for (int i = 0; i < failedPackets.size(); i++) {
        owner->stats.cpuRetryCountSend++;
        // grab next packet
        auto pkt = failedPackets.front();

        // try to send packet
        success = sendTimingResp(pkt);
        DPRINTF(AIMCtrl, "recvRespRetry %s, success=%d\n",
            formattedPacket(pkt), success);

        // remove packets which were successfully resent
        if (success)
            failedPackets.pop();
    }
    DPRINTF(AIMCtrl, "recvRespRetry %d failed packets in queue\n",
        failedPackets.size());
}

inline void
AIMCtrl::MemSidePort::processPacket(PacketPtr pkt)
{
    // NOTE: for now differentiate crypto writes and reads just by checking
    // this field
    // NOTE: ignore clean evicts!
    if (!pkt->isRead() && pkt->cmd != MemCmd::CleanEvict) {
        // perform crypto write postamble
        owner->opAIMWriteCallback(pkt);
    }
}

bool
AIMCtrl::MemSidePort::sendPacket(PacketPtr pkt)
{
    owner->stats.memTotalCountSend++;

    // make sure we cannot miss packets
    // NOTE: don't even attempt a timing req if the failure queue is not empty
    bool success = failedPackets.empty() && sendTimingReq(pkt);
    if (!success) {
        failedPackets.push(pkt);
        ++owner->stats.memFailuresCountSend;
    } else {
        processPacket(pkt);
    }

    DPRINTF(AIMCtrl, "sendPacket %s, success=%d, failed queue size=%d\n",
        formattedPacket(pkt), success, failedPackets.size());
    return success;
}

bool
AIMCtrl::MemSidePort::recvTimingResp(PacketPtr pkt)
{
    DPRINTF(AIMCtrl, "recvTimingResp %s\n", formattedPacket(pkt));
    owner->stats.memTotalCountRecv++;

    // just forward
    return owner->handleResponse(pkt);
}

void
AIMCtrl::MemSidePort::recvReqRetry()
{
    // use for loop to only process packets currently in queue,
    // ignorning newly added failed packets
    bool success = true;
    // since processPacket has side-effects, we want to call processPacket
    // after retrying failed packets
    std::queue<PacketPtr> successful_packets;
    while (success && !failedPackets.empty()) {
        owner->stats.memRetryCountSend++;
        // grab next packet
        // we want to keep packets which were not successfully resent
        auto pkt = failedPackets.front();

        // try to send packet
        success = sendTimingReq(pkt);
        DPRINTF(AIMCtrl, "recvReqRetry: %s, success=%d\n",
            formattedPacket(pkt), success);

        if (success) {
            // on success, delete packet
            failedPackets.pop();
            successful_packets.emplace(pkt);
        }
    }

    DPRINTF(AIMCtrl, "recvReqRetry: %d failed packets in queue\n",
        failedPackets.size());

    while (successful_packets.size() > 0) {
        processPacket(successful_packets.front());
        successful_packets.pop();
    }
}

void
AIMCtrl::MemSidePort::recvRangeChange()
{
    owner->sendRangeChange();
}

void
AIMCtrl::onCounterRead(PacketPtr pkt, std::vector<PacketId> data_ids,
    bool req_is_read)
{
    if (req_is_read) {
        DPRINTF(AIMCtrl,
            "(AIMRead) counter read"
            " completed for %s\n",
            formattedPacket(pkt));
        // schedule AES keystream generation
        scheduleAESDecryptOp(pkt, data_ids);
    } else {
        DPRINTF(AIMCtrl,
            "(AIMWrite) counter read"
            " completed for %s\n",
            formattedPacket(pkt));
        // schedule AES keystream generation
        scheduleAESEncryptOp(pkt, data_ids);
    }
}

void
AIMCtrl::onIntTRBCompletedRequest()
{
    if (!int_trb->is_busy() && cpu_failed_packets > 0) {
        DPRINTF(AIMCtrl,
            "onIntTRBCompletedRequest: %d failed packets, retrying\n",
            cpu_failed_packets);
        cpu_failed_packets--;
        cpuPort.sendRetryReq();
    }
}

void
AIMCtrl::opAIMWrite(PacketPtr pkt, std::vector<PacketId> data_ids)
{
    for (auto data_id : data_ids) {
        DPRINTF(AIMCtrl, "AIM write operation %s for data id %ld\n",
            formattedPacket(pkt), data_id);
        // if completing the write queue entry returns *true*, the data write
        // has previously already completed, so now is the appropriate time
        // to return the packet to the CPU
        // NOTE: for writes this should always be true
        if (write_queue.complete_counter_read(data_id)) {
            DPRINTF(AIMCtrl,
                "write_queue: completed counter read event for data pkt "
                "%ld\n",
                data_id);
            PacketPtr data_pkt = write_queue.get_data_pkt(data_id);
            memPort.sendPacket(data_pkt);
            write_queue.erase_data_read(data_id);
        }
    }
}

void
AIMCtrl::opAIMWriteCallback(PacketPtr pkt)
{
    scheduleMACOp(pkt, DataMACEventType::DataMACUpdate);
    // free up address again
    blocked_set.erase(blocked_set.find(pkt->getAddr()));
    DPRINTF(AIMCtrl,
        "opAIMWriteCallback: reduced block set to %d entries\n",
        blocked_set.size());

    onIntTRBCompletedRequest();
}

void
AIMCtrl::opAIMRead(PacketPtr pkt, std::vector<PacketId> data_ids)
{
    for (auto data_id : data_ids) {
        DPRINTF(AIMCtrl, "AIM read operation %s for data id %ld\n",
            formattedPacket(pkt), data_id);
        // if completing the read queue entry returns *true*, the data read
        // has previously already completed, so now is the appropriate time
        // to return the packet to the CPU
        if (read_queue.complete_counter_read(data_id)) {
            DPRINTF(AIMCtrl,
                "read_queue: completed counter read event for data pkt %ld\n",
                pkt->id);
            PacketPtr data_pkt = read_queue.get_data_pkt(data_id);
            bool success = cpuPort.sendPacket(data_pkt);
            panic_if(!success,
                "cpu port send packet is not allowed to fail\n");
            read_queue.erase_data_read(data_id);
        }
    }
}

void
AIMCtrl::opDataMACCheck(PacketPtr pkt)
{
    DPRINTF(AIMCtrl, "opDataMACCheck: completed\n");
    // theoretically we would check the MAC here
}

void
AIMCtrl::opDataMACUpdate(PacketPtr pkt)
{
    DPRINTF(AIMCtrl, "opDataMACUpdate: completed\n");
    // theoretically we would update the MAC here
}

} // namespace gem5
