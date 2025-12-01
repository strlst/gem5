#ifndef __MEMSEC_INT_TREE_HH__
#define __MEMSEC_INT_TREE_HH__

#include <cmath>
#include <cstdint>
#include <functional>
#include <queue>
#include <vector>

#include "base/addr_range.hh"
#include "base/statistics.hh"
#include "base/stats/units.hh"
#include "base/types.hh"
#include "mem/packet.hh"
#include "mem/port.hh"
#include "memsec/int_tree_req.hh"
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

class IntTRB : public SimObject
{
  private:
    System* sys;
    RequestorID requestorId;

    int32_t size;
    uint32_t bus_bytes;
    uint32_t packing_factor;
    uint32_t counter_bits;
    uint32_t counter_bytes;
    uint32_t tree_height;
    uint32_t tree_node_bytes;
    // this part is constant with respect to system instantiation
    const uint64_t non_leaf_nodes;
    AddrRange range_integrity;
    std::list<IntTreeReq> queue;
    // whether to simulate request merging strategy
    bool merge_requests;
    // whether to simulate request defragmentation strategy
    bool defragment_requests;
    // whether to simulate request defragmentation strategy
    bool par_dispatch;

    // identify requests
    uint64_t serial = 0;

    MACUnit* mac_unit;

    // counter read callback
    std::function<void(PacketPtr, std::vector<PacketId>, bool)>
        counter_read_callback;
    // release logic
    std::function<void()> release_callback;

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

        // called by the aim controller
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

    void register_counter_read_callback(
        std::function<void(PacketPtr, std::vector<PacketId>, bool)> callback);
    void register_release_callback(std::function<void()> callback);

    struct PktStats : public Group
    {
        statistics::Scalar enqueued;
        statistics::Scalar mdcacheTotalCountSend;
        statistics::Scalar mdcacheTotalCountRecv;
        statistics::Scalar mdcacheFailuresCountSend;
        statistics::Scalar mdcacheRetryCountSend;
        statistics::Scalar fullyMergedRequests;
        statistics::Scalar mergedRequests;
        statistics::Scalar mergedRequestNodes;
        statistics::Scalar defragmentedRequests;
        statistics::Scalar defragmentedRequestNodes;
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
                  "amount of request packets sent as a result of a retry"),
              ADD_STAT(fullyMergedRequests, statistics::units::Count::get(),
                  "amount of fully merged requests"),
              ADD_STAT(mergedRequests, statistics::units::Count::get(),
                  "amount of merged requests"),
              ADD_STAT(mergedRequestNodes, statistics::units::Count::get(),
                  "amount of merged request nodes"),
              ADD_STAT(defragmentedRequests, statistics::units::Count::get(),
                  "amount of defragmented requests"),
              ADD_STAT(defragmentedRequestNodes,
                  statistics::units::Count::get(),
                  "amount of defragmented request nodes")
        {
        }
    } stats;

    Port& getPort(const std::string& if_name, PortID idx);
    bool handleResponse(PacketPtr pkt);

    bool is_busy()
    {
        // prevent queue from filling up when set to -1
        return size >= 0 && queue.size() >= size;
    }

    void enqueue_request(Addr data_address, PacketId data_id, bool is_read);

    inline std::list<IntTreeReq>::iterator get_request_it_by_id(PacketId id);
    IntTreeReq& get_request_by_id(PacketId id);

    // print helpers
    void print_queue(std::list<IntTreeReq> queue);

    // dispatch logic
    void merge_from_queue();
    void defragment_from_queue();
    void dispatch_from_queue();

    // state change
    void update_metadata(PacketPtr old_pkt, PacketPtr new_pkt);
    void release_request(PacketId id);
    bool complete_request_node(PacketId id);
    void dispatch_request(IntTreeReq& request);

    // state query
    bool contains_request_node(PacketId id, bool read_flag);
    bool contains_request_node(PacketId id);

    // events
    void scheduleMACOp(PacketPtr pkt, IntegrityMACEventType type);
    void scheduleMDCacheSend(PacketPtr pkt, Tick delay);

    /**
     * Callback in case a metadata tree node has been checked for integrity
     *
     * @param pkt requesting packet
     */
    void IntegrityMACCheck(PacketPtr pkt);

    /**
     * In case a metadata tree node counter has been updated, perform
     * final MAC computation (by simulating the delay introduced by the
     * on-chip MAC engine)
     *
     * @param pkt requesting packet
     */
    void IntegrityMACUpdate(PacketPtr pkt);

    /**
     * Event callback for hassle-free configurable delays on sending packets
     * to the metadata cache.
     *
     * @param pkt requesting packet
     */
    void MDCacheSend(PacketPtr pkt);
};

};

#endif // __MEMSEC_INT_TREE_HH__
