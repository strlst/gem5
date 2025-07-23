#ifndef __MEMSEC_TREE_UPDATE_HH__
#define __MEMSEC_TREE_UPDATE_HH__

#include <cmath>
#include <cstdint>
#include <list>
#include <utility>

#include "base/addr_range.hh"
#include "base/types.hh"
#include "debug/IntTRB.hh"
#include "mem/packet.hh"
#include "params/IntTRB.hh"
#include "sim/sim_object.hh"

namespace gem5
{

struct IntTreeReqNode
{
    Addr address;
    uint8_t offset;
    bool completed = false;

    IntTreeReqNode(Addr address, uint8_t offset)
        : address(address), offset(offset)
    {
    }

    bool complete(Addr address)
    {
        // we want to return true only when we mark this request complete for
        // the first time
        return !completed && (completed = address == this->address);
        /*
        if (!completed) {
            completed = address == this->address;
            return true;
        }
        return false;
        */
    }
};

struct IntTreeReq
{
    Addr data_address;
    // complete | node address | node offset
    std::list<IntTreeReqNode> nodes =
        std::list<IntTreeReqNode>();
    uint8_t completed_layers = 0;

    IntTreeReq(Addr data_address) : data_address(data_address)
    {
        panic_if(sizeof(Addr) != sizeof(uint64_t), "unsupported addr size\n");
    }

    void add_request_node(Addr node_address, uint8_t node_offset)
    {
        nodes.emplace_back(
            IntTreeReqNode(node_address, node_offset));
    }

    bool complete(Addr node_address)
    {
        for (auto& node : nodes) {
            if (node.complete(node_address)) {
                completed_layers++;
                return true;
            }
        }
        return false;
    }

    bool contains_request_node(Addr node_address)
    {
        for (auto& node : nodes) {
            if (node.address == node_address) {
                return true;
            }
        }
        return false;
    }

    uint8_t get_offset(Addr node_address)
    {
        for (auto& node : nodes) {
            if (node.address == node_address) {
                return node.offset;
            }
        }
        panic("could not find node address 0x%x in request\n", node_address);
    }

    std::string to_string()
    {
        std::ostringstream ss;
        ss << "IntegrityTreeReq(";
        ss << "data_addr=0x" << std::hex << data_address << std::dec;
        for (auto node : nodes) {
            ss << ", node_addr=0x" << std::hex << node.address << std::dec
               << "@" << unsigned(node.offset) << (node.completed ? "*" : "");
        }
        ss << ", completed_layers=" << unsigned(completed_layers);
        ss << ")";
        return ss.str();
    }
};

class IntTRB : public SimObject
{
  private:
    uint8_t size;
    uint64_t bus_bytes;
    uint64_t packing_factor;
    uint64_t counter_bytes;
    uint64_t tree_height;
    uint64_t tree_node_bytes;
    // this part is constant with respect to system instantiation
    const uint64_t non_leaf_nodes;
    AddrRange range_integrity;
    std::list<IntTreeReq> queue;

  public:
    IntTRB(const IntTRBParams& params);

    bool is_full() { return queue.size() >= size; }

    std::pair<bool, IntTreeReq> enqueue_request(Addr data_address);
    IntTreeReq& find_request(Addr node_address);

    void update_metadata(PacketPtr pkt);
    bool contains_request_node(Addr node_address);
    bool complete_request_node(Addr node_address);
};

};

#endif // __MEMSEC_TREE_UPDATE_HH__
