#include <cstdint>

#include "base/trace.hh"
#include "debug/IntTRB.hh"
#include "tree_update.hh"

namespace gem5
{

IntTRB::IntTRB(const IntTRBParams& params)
    : SimObject(params), size(params.size), bus_bytes(params.bus_bytes),
      packing_factor(params.packing_factor),
      counter_bytes(params.counter_bytes), tree_height(params.tree_height),
      tree_node_bytes(params.tree_node_bytes),
      non_leaf_nodes(
          (std::pow(params.packing_factor, params.tree_height) - 1) /
          (params.packing_factor - 1)),
      range_integrity(params.range_integrity)
{
    DPRINTF(IntTRB, "Created integrity tree request buffer with properties\n");
    DPRINTF(IntTRB, "\t\t\t%d queue size\n", params.size);
    DPRINTF(IntTRB,
        "\t\t\t%d tree node bytes (%d height, %d counter bytes, %d non "
        "leaf nodes)\n",
        params.tree_node_bytes, params.tree_height, params.counter_bytes,
        non_leaf_nodes);
}

std::pair<bool, IntTreeReq>
IntTRB::enqueue_request(Addr data_address, bool is_read)
{
    // NOTE: the latency of address translation can probably be hidden in
    // case packing_factor is not a power of 2, and if it is, the
    // necessary operations can be performed with bit operations
    uint64_t node_id =
        non_leaf_nodes + data_address / bus_bytes / packing_factor;
    uint8_t node_offset = data_address / bus_bytes % packing_factor;
    IntTreeReq new_request = IntTreeReq(data_address, is_read);
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

        // if one of the nodes is currently busy, we cannot fulfill this
        // request
        if (contains_request_node(node_address))
            return std::make_pair(false, new_request);

        // add node to new request
        new_request.add_request_node(node_address, node_offset);

        // update node id
        uint64_t parent_id = node_id / packing_factor;
        node_offset = node_id % packing_factor;
        node_id = parent_id;
    }

    queue.emplace_back(new_request);
    return std::make_pair(true, new_request);
}

IntTreeReq&
IntTRB::find_request(Addr node_address)
{
    auto it = queue.begin();
    while (it != queue.end() && !it->contains_request_node(node_address))
        std::advance(it, 1);
    panic_if(it == queue.end(),
        "could not find element in integrity tree request buffer\n");
    return *it;
}

void
IntTRB::update_metadata(PacketPtr pkt)
{
    auto node_address = pkt->getAddr();
    auto request = find_request(node_address);
    uint8_t offset = request.get_offset(node_address);
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
        if (it->complete(node_address)) {
            DPRINTF(IntTRB, "finished metadata write 0x%x, %s\n",
                node_address, it->to_string());
            if (it->completed_layers >= tree_height) {
                // TODO: perform on-chip root node counter update
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
    for (auto it = queue.begin(); it != queue.end(); it++) {
        if (it->contains_request_node(node_address)) {
            panic_if(it->completed_layers < tree_height,
                "request was released even though it's not complete!\n");
            DPRINTF(IntTRB,
                "all metadata writes complete for %s, deleting "
                "request from integrity tree request buffer\n",
                it->to_string());
            queue.erase(it);
            return;
        }
    }
    panic("could not find element in integrity tree request buffer\n");
}

};
