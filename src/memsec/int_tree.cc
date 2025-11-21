#include "memsec/int_tree.hh"

#include <cstdint>
#include <sstream>

#include "base/trace.hh"
#include "debug/IntTRB.hh"
#include "memsec/aim_event.hh"
#include "memsec/int_tree_req.hh"
#include "memsec/util.hh"
#include "sim/cur_tick.hh"

namespace gem5
{

IntTRB::IntTRB(const IntTRBParams& params)
    : SimObject(params), sys(params.system),
      requestorId(sys->getRequestorId(this)), size(params.size),
      bus_bytes(params.bus_bytes), packing_factor(params.packing_factor),
      counter_bits(params.counter_bits),
      counter_bytes(params.counter_bits / 8), tree_height(params.tree_height),
      tree_node_bytes(params.tree_node_bytes),
      non_leaf_nodes(
          (std::pow(params.packing_factor, params.tree_height) - 1) /
          (params.packing_factor - 1)),
      range_integrity(params.range_integrity),
      merge_requests(params.merge_req),
      defragment_requests(params.defrag_req), mac_unit(params.mac_unit),
      mdcachePort(params.name + ".mdcache_side_port", this), stats(this)
{
    DPRINTF(IntTRB, "Created integrity tree request buffer with properties\n");
    DPRINTF(IntTRB, "\t\t\t%d queue size\n", params.size);
    DPRINTF(IntTRB,
        "\t\t\t%d tree node bytes (%d height, %d counter bits, %d non "
        "leaf nodes)\n",
        params.tree_node_bytes, params.tree_height, params.counter_bits,
        non_leaf_nodes);
}

Port&
IntTRB::getPort(const std::string& if_name, PortID idx)
{
    panic_if(idx != InvalidPortID, "This object doesn't support vector ports");

    // this is the name from the Python SimObject declaration (AIMCtrl.py)
    if (if_name == "metadata_cache_side_port") {
        return mdcachePort;
    } else {
        // pass it along to our super class
        return SimObject::getPort(if_name, idx);
    }
}

bool
IntTRB::MetadataCacheSidePort::sendPacket(PacketPtr pkt)
{
    owner->stats.mdcacheTotalCountSend++;

    // make sure we cannot miss packets
    // don't even attempt a timing req if the failure queue is not empty
    bool success = failedPackets.empty() && sendTimingReq(pkt);
    if (!success) {
        failedPackets.push(pkt);
        ++owner->stats.mdcacheFailuresCountSend;
    }
    DPRINTF(IntTRB, "sendPacket %s, success=%d\n", formattedPacket(pkt),
        success);

    return success;
}

bool
IntTRB::MetadataCacheSidePort::recvTimingResp(PacketPtr pkt)
{
    DPRINTF(IntTRB, "recvTimingResp %s\n", formattedPacket(pkt));
    owner->stats.mdcacheTotalCountRecv++;

    // just forward
    return owner->handleResponse(pkt);
}

void
IntTRB::MetadataCacheSidePort::recvReqRetry()
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
        DPRINTF(IntTRB, "recvReqRetry %s, success=%d\n", formattedPacket(pkt),
            success);
        // keep packets which were not successfully resent
        if (!success)
            failedPackets.push(pkt);
    }
    DPRINTF(IntTRB, "recvReqRetry %d failed packets in queue\n",
        failedPackets.size());
}

void
IntTRB::MetadataCacheSidePort::recvRangeChange()
{
    owner->sendRangeChange();
}

bool
IntTRB::handleResponse(PacketPtr pkt)
{
    if (pkt->isRead()) {
        // depending on whether we are just checking the integrity tree,
        // or reading to update the integrity tree metadata, we have to
        // react differently
        if (contains_request_node(pkt->getAddr(), true)) {
            // the node we are handling is contained in a read request
            scheduleMACOp(pkt, IntegrityMACEventType::IntegrityMACCheck);
        } else {
            // the node we are handling is contained in a write request
            // the cache has yielded the data, now we update the counters
            // and hash
            PacketPtr new_pkt = createPktFromPkt(pkt, MemCmd::WriteReq);
            DPRINTF(IntTRB,
                "created write-after-read request packet %s\n",
                formattedPacket(new_pkt));
            update_metadata(new_pkt);
            // schedule cache response for next cycle
            scheduleMDCacheSend(new_pkt, AFTER_1_CYCLE(curTick()));
        }
    } else {
        // forward completion event to queue
        bool completed = complete_request_node(pkt->getAddr());
        if (completed) {
            scheduleMACOp(pkt, IntegrityMACEventType::IntegrityMACUpdate);
        }
    }

    return true;
}

void
IntTRB::merge_from_queue()
{
    // left merge strategy, combine common (upper) nodes and fill
    // request slots with remaining requests
    if (queue.size() > 1) {
        //print_queue(queue);
        // reverse iteration
        auto right = queue.rbegin();
        auto left = std::next(right);
        for (; left != queue.rend(); left++, right++) {
            if (!left->dispatched && left->is_read == right->is_read) {
                int merged = left->merge_from(*right);
                if (merged > 0)
                    DPRINTF(IntTRB,
                        "merged %d nodes from request %ld into %ld\n", merged,
                        left->serial, right->serial);
                stats.mergedRequests++;
                stats.mergedRequestNodes += merged;
            }
        }
        // sweep queue again to delete fully merged (empty) requests
        for (auto it = queue.begin(); it != queue.end();) {
            if (it->nodes.size() == 0) {
                stats.fullyMergedRequests++;
                DPRINTF(IntTRB, "deleting fully merged request %ld\n",
                    it->serial);
                it = queue.erase(it);
            } else {
                it++;
            }
        }
        //printf("post-merge ");
        //print_queue(queue);
        //printf("\n");
    }
}

void
IntTRB::defragment_from_queue()
{
    if (defragment_requests) {
        auto left = queue.begin();
        auto right = std::next(left);
        while (right != queue.end()) {
            if (left->nodes.size() + right->nodes.size() <= tree_height &&
                left->is_read == right->is_read) {
                stats.defragmentedRequests++;
                stats.defragmentedRequestNodes += right->nodes.size();
                DPRINTF(IntTRB,
                    "defragmenting requests %ld (%ld elements) and %ld (%ld "
                    "elements)\n",
                    left->serial, left->nodes.size(), right->serial,
                    right->nodes.size());
                for (auto req : right->nodes)
                    left->nodes.emplace_back(req);
                right = queue.erase(right);
            } else {
                right++;
            }
            left++;
        }
    }
}

void
IntTRB::dispatch_from_queue()
{
    if (merge_requests)
        merge_from_queue();
    if (defragment_requests)
        defragment_from_queue();
    // simple dispatch logic: just dispatch front
    if (queue.size() > 0 && !queue.front().dispatched)
        dispatch_request(queue.front());
}

void
IntTRB::dispatch_request(IntTreeReq& request)
{
    // sanity check
    panic_if(request.dispatched,
        "cannot dispatch already dispatched request!\n");

    request.dispatched = true;
    for (auto& node : request.nodes) {
        // create and send packets for each layer
        PacketPtr packet_fetch_md = createPkt(node.address, tree_node_bytes,
            requestorId, MemCmd::ReadReq);
        // schedule cache requests for next cycle
        scheduleMDCacheSend(packet_fetch_md, AFTER_1_CYCLE(curTick()));
    }
}

void
IntTRB::register_release_callback(std::function<void()> callback)
{
    release_callback = callback;
}

void
IntTRB::enqueue_request(Addr data_address, bool is_read)
{
    // keep statistics
    stats.enqueued++;

    panic_if(range_integrity.contains(data_address),
        "integrity tree cannot translate address 0x%x falling into the "
        "integrity range %s!\n",
        data_address, range_integrity.to_string());

    // check size constraint
    panic_if(size >= 0 && queue.size() > size,
        "int trb is not allowed to exceed %d elements\n", size);

    // create request
    IntTreeReq new_request = IntTreeReq(serial++, data_address, is_read);

    // NOTE: the latency of address translation can probably be hidden in
    // case packing_factor is not a power of 2, and if it is, the
    // necessary operations can be performed with bit operations
    uint64_t node_id =
        non_leaf_nodes + data_address / bus_bytes / packing_factor;
    uint8_t node_offset = data_address / bus_bytes % packing_factor;
    for (int i = 0; i < tree_height; i++) {
        // compute actual node address
        Addr node_address =
            range_integrity.start() + node_id * tree_node_bytes;
        DPRINTF(IntTRB,
            "translating address 0x%x -> integrity tree address=0x%x, "
            "nodeid=%d, offset=%d @ integrity tree level %d\n",
            data_address, node_address, node_id, node_offset, i);
        panic_if(!range_integrity.contains(node_address),
            "the node address 0x%x must lie within the integrity memory "
            "range %s\n",
            node_address, range_integrity.to_string());

        // add node to new request
        new_request.add_request_node(node_address, node_offset);

        // update node id
        uint64_t parent_id = node_id / packing_factor;
        node_offset = node_id % packing_factor;
        node_id = parent_id;
    }

    // finally add request to queue
    DPRINTF(IntTRB,
        "enqueueing 0x%x (read=%d) to buffer with %d entries (max %d)\n",
        data_address, is_read, queue.size(), size);
    queue.emplace_back(new_request);

    // dispatch all dispatchable requests
    dispatch_from_queue();
}

inline std::list<IntTreeReq>::iterator
IntTRB::get_request_it(Addr node_address)
{
    auto it = queue.begin();
    while (it != queue.end() && !it->contains_request_node(node_address))
        std::advance(it, 1);
    panic_if(it == queue.end(),
        "could not find element in integrity tree request buffer\n");
    return it;
}

IntTreeReq&
IntTRB::get_request(Addr node_address)
{
    return *get_request_it(node_address);
}

void
IntTRB::update_metadata(PacketPtr pkt)
{
    auto node_address = pkt->getAddr();
    auto request = get_request(node_address);
    Diffs& diffs = request.get_diffs(node_address);
    // TODO: update offset calculation and subsequent counter value update
    uint8_t offset = diffs.begin()->first;
    DPRINTF(IntTRB, "updating 0x%x request %s offset 0x%x\n", node_address,
        request.to_string(), offset);
    // next we want to update the counter at position offset, in a
    // way that is generic with respect to counter size
    // (byte granularity)
    uint8_t* data = pkt->getPtr<uint8_t>();
    uint8_t carry = 1;
    uint8_t rightmost = offset * counter_bytes;
    for (int k = counter_bytes - 1; k >= 0 && carry == 1; k--) {
        data[rightmost + k] += carry;
        // we only carry if there has been an overflow
        carry = data[rightmost + k] == 0;
    }
    pkt->cmd = MemCmd::WriteReq;
}

bool
IntTRB::contains_request_node(Addr node_address, bool read_flag)
{
    for (auto& request : queue) {
        if (request.contains_request_node(node_address) &&
            request.is_read == read_flag)
            return true;
    }
    return false;
};

bool
IntTRB::contains_request_node(Addr node_address)
{
    for (auto& request : queue) {
        if (request.contains_request_node(node_address))
            return true;
    }
    return false;
};

bool
IntTRB::complete_request_node(Addr node_address)
{
    for (auto it = queue.begin(); it != queue.end(); it++) {
        // skip undispatched nodes
        if (!it->dispatched)
            continue;

        if (it->complete(node_address)) {
            DPRINTF(IntTRB, "finished metadata %s 0x%x, %s\n",
                it->is_read ? "read" : "write", node_address,
                it->to_string());
            if (it->completed_layers >= it->nodes.size()) {
                // NOTE: perform on-chip root node counter update
                // this can be done always in a single cycle since each node
                // is an on-chip register by design
                return true;
            }
            return false;
        }
    }
    panic("could not find element in integrity tree request buffer\n");
}

void
IntTRB::release_request(Addr node_address)
{
    // NOTE: since the dispatched node is always the front, the parameter
    // is actually not needed to search the right node to release, but this
    // could change with a more sophisticated dispatch logic

    // make sure to free addresses from set
    auto front = queue.front();

    panic_if(front.completed_layers < front.nodes.size(),
        "request %d was released even though it's not complete!"
        "(%d < %d)\n",
        front.serial, front.completed_layers, tree_height);
    DPRINTF(IntTRB,
        "all metadata writes complete for %s, deleting "
        "request from integrity tree request buffer\n",
        front.to_string());

    /*
    // print nodes and its counter tree translations
    printf("%lx ~", front.data_address);
    for (auto r : front.nodes) {
        printf(" %lx", r.address);
    }
    printf("\n");
*/

    DPRINTF(IntTRB, "dequeueing 0x%x (read=%d) from buffer with %d entries\n",
        front.data_address, front.is_read, queue.size());

    // finally remove element
    queue.pop_front();

    // dispatch all dispatchable requests
    dispatch_from_queue();

    // call on CPU callback
    release_callback();
}

void
IntTRB::IntegrityMACCheck(PacketPtr pkt)
{
    DPRINTF(IntTRB, "finished integrity MAC check %s\n", formattedPacket(pkt));
    bool completed = complete_request_node(pkt->getAddr());
    if (completed) {
        release_request(pkt->getAddr());
    }
}

void
IntTRB::IntegrityMACUpdate(PacketPtr pkt)
{
    DPRINTF(IntTRB, "finished integrity MAC update %s\n",
        formattedPacket(pkt));
    // theoretically we would update the MAC here
    release_request(pkt->getAddr());
}

void
IntTRB::MDCacheSend(PacketPtr pkt)
{
    mdcachePort.sendPacket(pkt);
}

void
IntTRB::scheduleMACOp(PacketPtr pkt, IntegrityMACEventType type)
{
    schedule(new IntegrityMACEvent(this, pkt, type),
        mac_unit->get_earliest_ready_time(pkt));
}

void
IntTRB::scheduleMDCacheSend(PacketPtr pkt, Tick delay)
{
    schedule(new MDCacheSendEvent(this, pkt), delay);
}

void
IntTRB::sendRangeChange()
{
    // no CPU to send range change to
}

void
IntTRB::print_queue(std::list<IntTreeReq> queue)
{
    std::ostringstream ss;
    ss << "queue state\n";
    for (auto r : queue) {
        ss << "  - " << r.to_string() << std::endl;
    }
    DPRINTF(IntTRB, "%s\n", ss.str());
}

};
