#ifndef __MEMSEC_AIM_CTRL_HH__
#define __MEMSEC_AIM_CTRL_HH__

#include <cstdint>
#include <queue>

#include "base/addr_range.hh"
#include "base/logging.hh"
#include "base/statistics.hh"
#include "base/stats/group.hh"
#include "base/types.hh"
#include "mem/packet.hh"
#include "mem/port.hh"
#include "memsec/aes_unit.hh"
#include "memsec/aim_queue.hh"
#include "memsec/int_tree.hh"
#include "memsec/mac_unit.hh"
#include "params/AIMCtrl.hh"
#include "sim/clocked_object.hh"

namespace gem5
{

enum DataMACEventType
{
    DataMACCheck,
    DataMACUpdate,
};

struct TreeCheckRequest
{
    Addr data_address;
    Addr node_address;
    uint64_t offset;
    uint8_t completed_layers = 0;
    bool violation = false;
};

/**
 * A very simple controller.
 */
class AIMCtrl : public ClockedObject
{
  public:
    /** constructor
     */
    AIMCtrl(const AIMCtrlParams& params);

    /**
     * Get a port with a given name and index. This is used at
     * binding time and returns a reference to a protocol-agnostic
     * port.
     *
     * @param if_name Port name
     * @param idx Index in the case of a VectorPort
     *
     * @return A reference to the given port
     */
    Port&
    getPort(const std::string& if_name, PortID idx = InvalidPortID) override;

  private:
    System* sys;
    RequestorID requestorId;

    // keep track of failed packets
    uint64_t cpu_failed_packets = 0;

    // request dimensioning
    uint64_t counter_bytes;

    uint64_t packing_factor;
    uint64_t bytes_per_address;

    uint64_t tree_height;
    uint64_t tree_node_bytes;

    uint64_t bus_bytes;

    AddrRange range_total;
    AddrRange range_data;
    AddrRange range_integrity;
    AddrRange range_leaves;

    AESUnit* aes_unit;
    MACUnit* mac_unit;

    AIMQueue read_queue;
    AIMQueue write_queue;
    std::unordered_multiset<Addr> blocked_set;
    IntTRB* int_trb;

    void startup() override;

    /**
     * Progress the controller one clock cycle.
     */
    void tick();

    /**
     * Event to schedule clock ticks.
     */
    EventFunctionWrapper tickEvent;

    /**
     * Handle the request from the CPU side.
     *
     * @param pkt requesting packet
     * @return true if we can handle the request this cycle, false if the
     *         requestor needs to retry later
     */
    bool handleRequest(PacketPtr pkt);

    /**
     * Handle the respone from the memory side.
     *
     * @param pkt responding packet
     * @return true if we can handle the response this cycle, false if the
     *         responder needs to retry later
     */
    bool handleResponse(PacketPtr pkt);

    /**
     * Handle a packet functionally. Update the data on a write and get the
     * data on a read.
     *
     * @param packet to functionally handle
     */
    void handleFunctional(PacketPtr pkt);

    /**
     * Return the address ranges this memobj is responsible for. Just use the
     * same as the next upper level of the hierarchy.
     *
     * @return the address ranges this memobj is responsible for
     */
    AddrRangeList getAddrRanges() const;

    /**
     * Tell the CPU side to ask for our memory ranges.
     */
    void sendRangeChange();

    // available operations
    void scheduleAESEncryptOp(PacketPtr pkt, std::vector<PacketId> data_ids);
    void scheduleAESDecryptOp(PacketPtr pkt, std::vector<PacketId> data_ids);
    void scheduleMACOp(PacketPtr pkt, DataMACEventType type);

    struct PktStats : public Group
    {
        statistics::Scalar totalRequests;
        statistics::Scalar totalReads;
        statistics::Scalar totalWrites;
        statistics::Scalar successfulRequests;
        statistics::Scalar successfulReads;
        statistics::Scalar successfulWrites;
        statistics::Scalar refusedRequests;
        statistics::Scalar refusedReads;
        statistics::Scalar refusedWrites;
        statistics::Scalar cpuTotalCountSend;
        statistics::Scalar cpuTotalCountRecv;
        statistics::Scalar memTotalCountSend;
        statistics::Scalar memTotalCountRecv;
        statistics::Scalar cpuFailuresCountSend;
        statistics::Scalar cpuRetryCountSend;
        statistics::Scalar memFailuresCountSend;
        statistics::Scalar memRetryCountSend;
        PktStats(Group* parent)
            : Group(parent),
              ADD_STAT(totalRequests, statistics::units::Count::get(),
                  "total amount of requests"),
              ADD_STAT(totalReads, statistics::units::Count::get(),
                  "total amount of read requests"),
              ADD_STAT(totalWrites, statistics::units::Count::get(),
                  "total amount of write requests"),
              ADD_STAT(successfulRequests, statistics::units::Count::get(),
                  "amount of successful requests"),
              ADD_STAT(successfulReads, statistics::units::Count::get(),
                  "amount of successful read requests"),
              ADD_STAT(successfulWrites, statistics::units::Count::get(),
                  "amount of successful write requests"),
              ADD_STAT(refusedRequests, statistics::units::Count::get(),
                  "amount of cpu side requests that were refused"),
              ADD_STAT(refusedReads, statistics::units::Count::get(),
                  "amount of cpu side read requests that were refused"),
              ADD_STAT(refusedWrites, statistics::units::Count::get(),
                  "amount of cpu side write requests that were refused"),
              ADD_STAT(cpuTotalCountSend, statistics::units::Count::get(),
                  "amount of sent packets"),
              ADD_STAT(cpuTotalCountRecv, statistics::units::Count::get(),
                  "amount of received packets"),
              ADD_STAT(memTotalCountSend, statistics::units::Count::get(),
                  "amount of sent packets"),
              ADD_STAT(memTotalCountRecv, statistics::units::Count::get(),
                  "amount of received packets"),
              ADD_STAT(cpuFailuresCountSend, statistics::units::Count::get(),
                  "amount of response packets which failed to send"),
              ADD_STAT(cpuRetryCountSend, statistics::units::Count::get(),
                  "amount of response packets sent as a result of a retry"),
              ADD_STAT(memFailuresCountSend, statistics::units::Count::get(),
                  "amount of request packets which failed to send"),
              ADD_STAT(memRetryCountSend, statistics::units::Count::get(),
                  "amount of request packets sent as a result of a retry")
        {
        }
    } stats;

    class MetadataCacheSidePort : public RequestPort
    {
      private:
        AIMCtrl* owner;

        // store packets for retries
        std::queue<PacketPtr> failedPackets;

      public:
        MetadataCacheSidePort(const std::string& name, AIMCtrl* owner)
            : RequestPort(name), owner(owner)
        {
        }

        // called by the AIM controller
        bool sendPacket(PacketPtr pkt);

      protected:
        // called by the mem side controller when responding
        bool recvTimingResp(PacketPtr pkt) override;
        // called by the mem side controller when responding
        void recvReqRetry() override;
        void recvRangeChange() override;
    };

    class CPUSidePort : public ResponsePort
    {
      private:
        AIMCtrl* owner;

        // store packets for retries
        std::queue<PacketPtr> failedPackets;

      public:
        CPUSidePort(const std::string& name, AIMCtrl* owner)
            : ResponsePort(name), owner(owner)
        {
        }

        // called by the AIM controller
        bool sendPacket(PacketPtr pkt);
        bool hasFailedPackets();
        AddrRangeList getAddrRanges() const override;
      protected:
        Tick recvAtomic(PacketPtr pkt) override
        {
            panic("recvAtomic unimpl.");
        }

        // called by the cpu side controller
        void recvFunctional(PacketPtr pkt) override;
        // called by the cpu side controller
        bool recvTimingReq(PacketPtr pkt) override;
        // called by the cpu side controller
        void recvRespRetry() override;
    };

    class MemSidePort : public RequestPort
    {
      private:
        /// The object that owns this object (AIMCtrl)
        AIMCtrl* owner;

        // store packet for retries
        std::queue<PacketPtr> failedPackets;

      public:
        MemSidePort(const std::string& name, AIMCtrl* owner)
            : RequestPort(name), owner(owner)
        {
        }

        // helper to deduplicate successful and failed packet code paths
        inline void processPacket(PacketPtr pkt);

        // called by the cpu side controller to actually transmit packets
        bool sendPacket(PacketPtr pkt);

        // keep track of ongoing recvReqRetry
        bool currentlyRetrying = false;

      protected:
        // called by the mem side controller when responding
        bool recvTimingResp(PacketPtr pkt) override;
        // called by the mem side controller when responding
        void recvReqRetry() override;
        void recvRangeChange() override;
    };

    // instantiation of the CPU-side ports
    CPUSidePort cpuPort;

    // instantiation of the memory-side port
    MemSidePort memPort;

  public:

    /**
     * Create a Packet from given parameters.
     */
    PacketPtr createPkt(Addr addr, size_t size, MemCmd cmd);

    /**
     * Create a Packet copy with a different command.
     */
    PacketPtr createPktFromPkt(PacketPtr pkt, MemCmd cmd);

    /**
     * Callback to hook on completion of Int TRB request.
     * Can be used, for instance, if there are failed packets from the CPU
     * side (due to full buffers or similar).
     */
    void onIntTRBCompletedRequest();

    /**
     * Callback to hook on completion of counter read.
     */
    void onCounterRead(PacketPtr pkt, std::vector<PacketId> data_ids,
        bool req_is_read);

    /**
     * If data sent from the CPU side needs to be encrypted, emulate
     * encryption feature functionally and also its timing.
     *
     * @param pkt requesting packet
     * @param data_ids packet ids of the associated data pkts
     * (not the same as pkt!)
     */
    void opAIMWrite(PacketPtr pkt, std::vector<PacketId> data_ids);
    void opAIMWriteCallback(PacketPtr pkt);

    /**
     * If data sent from the memory side needs to be decrypted, emulate
     * encryption feature functionally and also its timing.
     *
     * @param pkt requesting packet
     * @param data_id packet ids of the associated data pkts
     * (not the same as pkt!)
     */
    void opAIMRead(PacketPtr pkt, std::vector<PacketId> data_ids);

    /**
     * In case a data node has been decrypted, perform MAC computation to
     * check integrity of the data itself
     *
     * @param pkt requesting packet
     */
    void opDataMACCheck(PacketPtr pkt);

    /**
     * In case a data node has been encrypted, perform MAC computation for
     * integrity of the data value itself (in addition to counter value
     * integrity provided by the counter tree)
     *
     * @param pkt requesting packet
     */
    void opDataMACUpdate(PacketPtr pkt);
};

} // namespace gem5

#endif // __MEMSEC_AIM_CTRL_HH__
