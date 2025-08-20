#ifndef __MEMSEC_TREE_UPDATE_HH__
#define __MEMSEC_TREE_UPDATE_HH__

#include <cmath>
#include <cstdint>
#include <list>
#include <queue>

#include "base/addr_range.hh"
#include "base/statistics.hh"
#include "base/types.hh"
#include "mem/packet.hh"
#include "mem/port.hh"
#include "memsec/mac_unit.hh"
#include "params/IntTRB.hh"
#include "sim/sim_object.hh"
#include "sim/system.hh"

namespace gem5
{

enum IntegrityMACEventType
{
    IntegrityMACCheck,
    IntegrityMACUpdate,
};

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
        // the first time, subsequent times return false
        // touching this code is likely unwise
        return !completed && (completed = address == this->address);
    }
};

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

    IntTreeReq(uint64_t serial, Addr data_address, bool is_read)
        : serial(serial), data_address(data_address), is_read(is_read)
    {
        panic_if(sizeof(Addr) != sizeof(uint64_t), "unsupported addr size\n");
    }

    void add_request_node(Addr node_address, uint8_t node_offset)
    {
        nodes.emplace_back(IntTreeReqNode(node_address, node_offset));
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
        ss << "serial=" << serial;
        ss << ", data_addr=0x" << std::hex << data_address << std::dec;
        ss << ", is_read=" << unsigned(is_read);
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
    System* sys;
    RequestorID requestorId;

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

    // identify requests
    uint64_t serial = 0;

    MACUnit* mac_unit;

    class MetadataCacheSidePort : public RequestPort
    {
      private:
        IntTRB* owner;

        // store packets for retries
        std::queue<PacketPtr> failedPackets;

      public:
        MetadataCacheSidePort(const std::string& name, IntTRB* owner)
            : RequestPort(name), owner(owner)
        {
        }

        // called by the crypto controller
        bool sendPacket(PacketPtr pkt);

      protected:
        // called by the mem side controller when responding
        bool recvTimingResp(PacketPtr pkt) override;
        // called by the mem side controller when responding
        void recvReqRetry() override;
        void recvRangeChange() override;
    };

    // instantiation of the CPU-side ports
    MetadataCacheSidePort mdcachePort;

    void sendRangeChange();

  public:
    IntTRB(const IntTRBParams& params);

    struct PktStats : public Group
    {
        statistics::Scalar enqueued;
        statistics::Scalar mdcacheTotalCountSend;
        statistics::Scalar mdcacheTotalCountRecv;
        statistics::Scalar mdcacheFailuresCountSend;
        statistics::Scalar mdcacheRetryCountSend;
        PktStats(Group* parent)
            : Group(parent),
              ADD_STAT(enqueued, statistics::units::Count::get(),
                  "amount of enqueued int trb requests"),
              ADD_STAT(mdcacheTotalCountSend, statistics::units::Count::get(),
                  "amount of sent packets"),
              ADD_STAT(mdcacheTotalCountRecv, statistics::units::Count::get(),
                  "amount of received packets"),
              ADD_STAT(mdcacheFailuresCountSend,
                  statistics::units::Count::get(),
                  "amount of request packets which failed to send"),
              ADD_STAT(mdcacheRetryCountSend, statistics::units::Count::get(),
                  "amount of request packets sent as a result of a retry")
        {
        }
    } stats;

    Port& getPort(const std::string& if_name, PortID idx);
    bool handleResponse(PacketPtr pkt);

    bool is_full() { return queue.size() >= size; }

    //std::pair<bool, IntTreeReq>
    void enqueue_request(Addr data_address, bool is_read);

    inline std::list<IntTreeReq>::iterator get_request_it(Addr node_address);
    IntTreeReq& get_request(Addr node_address);

    // state change
    void update_metadata(PacketPtr pkt);
    void release_request(Addr node_addr);
    bool complete_request_node(Addr node_address);

    // state query
    bool contains_request_node(Addr node_address, bool read_flag);
    bool contains_request_node(Addr node_address);

    // events
    void scheduleMACOp(PacketPtr pkt, IntegrityMACEventType type);
    void IntegrityMACCheck(PacketPtr pkt);
    void IntegrityMACUpdate(PacketPtr pkt);
};

};

#endif // __MEMSEC_TREE_UPDATE_HH__
