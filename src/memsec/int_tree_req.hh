#ifndef __MEMSEC_INT_TREE_REQ_HH__
#define __MEMSEC_INT_TREE_REQ_HH__

#include <cstdint>
#include <list>
#include <sstream>
#include <utility>

#include "base/logging.hh"
#include "base/types.hh"
#include "mem/packet.hh"

namespace gem5
{

// convenient typedef for offset/value pairs
typedef std::unordered_map<uint8_t, uint8_t> Diffs;

struct IntTreeReqNode
{
    Addr address;
    Diffs diffs;
    PacketId id{};
    bool completed = false;

    IntTreeReqNode(Addr address, uint8_t offset) : address(address)
    {
        diffs[offset] = 1;
    }

    bool complete_by_id(PacketId id)
    {
        // we want to return true only when we mark this request complete for
        // the first time, subsequent times return false
        // touching this code is likely unwise
        return !completed && (completed = id == this->id);
    }
};

inline std::ostream&
operator<<(std::ostream& os, const IntTreeReqNode& node)
{
    os << std::hex << node.address << std::dec << "@n" << node.diffs.size();
    for (auto& [offset, count] : node.diffs) {
        os << "d" << unsigned(offset) << "+" << unsigned(count);
    }
    os << (node.completed ? "*" : "");
    return os;
}

struct IntTreeReq
{
    // integer identifying sequential causality
    uint64_t serial;
    // address of actual physical memory being protected
    Addr data_address;
    // each request can be a write or read request (tree update or tree check)
    bool is_read;
    // each integrity tree request encompasses a path of nodes from the leaf
    // up to the node before the root
    std::list<IntTreeReqNode> nodes = std::list<IntTreeReqNode>();
    uint8_t completed_layers = 0;
    bool dispatched = false;

    IntTreeReq(uint64_t serial, Addr data_address, bool is_read)
        : serial(serial), data_address(data_address), is_read(is_read)
    {
        panic_if(sizeof(Addr) != sizeof(uint64_t), "unsupported addr size\n");
    }

    void add_request_node(Addr node_address, uint8_t node_offset)
    {
        nodes.emplace_back(IntTreeReqNode(node_address, node_offset));
    }

    void update_packet_id(PacketId old_id, PacketId new_id)
    {
        for (auto& node : nodes) {
            if (node.id == old_id) {
                node.id = new_id;
                return;
            }
        }
        panic("could not find packet id %ld in request %s\n", old_id,
            to_string());
    }

    bool complete_by_id(PacketId id)
    {
        for (auto& node : nodes) {
            if (node.complete_by_id(id)) {
                completed_layers++;
                return true;
            }
        }
        return false;
    }

    bool contains_request_node_by_id(PacketId id)
    {
        for (auto& node : nodes) {
            if (node.id == id) {
                return true;
            }
        }
        return false;
    }

    Diffs& get_diffs_by_id(PacketId id)
    {
        for (auto& node : nodes) {
            if (node.id == id) {
                return node.diffs;
            }
        }
        panic("could not find packet id %ld in request %s\n", id, to_string());
    }

    float get_overlap(IntTreeReq& comp)
    {
        auto s_it = nodes.begin();
        auto c_it = comp.nodes.begin();

        uint8_t identical = 0;
        for (int i = 0; i < nodes.size(); i++) {
            identical += s_it->address == c_it->address;
            s_it++;
            c_it++;
        }
        return ((float)identical) / ((float)nodes.size());
    }

    int merge_from(IntTreeReq& comp)
    {
        int merged = 0;
        // merge comp request nodes into current request
        auto s_it = nodes.begin();
        auto c_it = comp.nodes.begin();
        while (s_it != nodes.end() && c_it != comp.nodes.end()) {
            //std::cout << *s_it << "<->" << *c_it;
            if (s_it->address == c_it->address) {
                for (auto d : c_it->diffs) {
                    s_it->diffs[d.first] += d.second;
                }
                c_it = comp.nodes.erase(c_it);
                //std::cout << " merged" << std::endl;
                merged++;
            } else {
                c_it++;
                //std::cout << std::endl;
            }
            s_it++;
        }
        return merged;
    }

    std::string to_string()
    {
        std::ostringstream ss;
        ss << "IntegrityTreeReq(";
        ss << "serial=" << serial;
        ss << ", data_addr=0x" << std::hex << data_address << std::dec;
        ss << ", is_read=" << unsigned(is_read);
        ss << ", nodes={ " << unsigned(is_read);
        for (auto& node : nodes)
            ss << node << ", ";
        ss << "}, length=" << nodes.size();
        ss << ", dispatched=" << unsigned(dispatched);
        ss << ", completed_layers=" << unsigned(completed_layers);
        ss << ")";
        return ss.str();
    }
};

}

#endif // __MEMSEC_INT_TREE_REQ_HH__
