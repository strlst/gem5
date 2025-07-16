#ifndef __MEMSEC_CRYPTO_CTRL_HH__
#define __MEMSEC_CRYPTO_CTRL_HH__

#include <cstdint>
#include <iterator>
#include <list>
#include <queue>
#include <utility>

#include "base/addr_range.hh"
#include "base/logging.hh"
#include "base/statistics.hh"
#include "base/stats/group.hh"
#include "base/types.hh"
#include "debug/CryptoCtrl.hh"
#include "mem/packet.hh"
#include "mem/port.hh"
#include "params/CryptoCtrl.hh"
#include "sim/clocked_object.hh"

#define TICK_PER_CYCLE 1000
#define AFTER_1_CYCLE(t) ((t) + TICK_PER_CYCLE)
#define AFTER_N_CYCLES(t, n) ((t) + ((n) * TICK_PER_CYCLE))

// parameters sourced from https://ieeexplore.ieee.org/document/7019004
// AES supports 128 bit blocks
//#define AES_BLOCK_BYTES (128 / 8)
// cycle latencies
//#define AES_ENC_CYCLES 336
//#define AES_DEC_CYCLES 216
// initiation intervals
//#define AES_ENC_II 336
//#define AES_DEC_II 216

namespace gem5
{

typedef std::pair<Tick, PacketPtr> DelayedPacket;

enum ResponseSource
{
    MemoryController,
    MetadataCache
};

struct TreeUpdateRequest
{
    Addr data_address;
    std::list<Addr> node_addresses = std::list<Addr>();
    std::list<uint8_t> node_offsets = std::list<uint8_t>();
    uint8_t completed_layers = 0;

    TreeUpdateRequest(Addr data_address) : data_address(data_address)
    {
        panic_if(sizeof(Addr) != sizeof(uint64_t), "unsupported addr size\n");
    }

    void add_request_node(Addr node_address, uint8_t node_offset)
    {
        node_addresses.emplace_back(node_address);
        node_offsets.emplace_back(node_offset);
    }

    bool contains_request_node(Addr node_address)
    {
        for (auto& addr : node_addresses) {
            if (addr == node_address) {
                return true;
            }
        }
        return false;
    }

    std::string to_string()
    {
        std::ostringstream ss;
        ss << "treeUpdateReq(";
        ss << "data_addr=0x" << std::hex << data_address << std::dec;
        for (auto address : node_addresses) {
            ss << ", node_addr=0x" << std::hex << address << std::dec;
        }
        ss << ", completed_layers=" << unsigned(completed_layers);
        ss << ")";
        return ss.str();
    }
};

struct TreeUpdateQueue
{
    uint8_t size;
    uint64_t bus_bytes;
    uint64_t packing_factor;
    uint64_t tree_height;
    uint64_t tree_node_bytes;
    // this part is constant with respect to system instantiation
    const uint64_t non_leaf_nodes;
    AddrRange range_integrity;
    std::list<TreeUpdateRequest> queue;

    TreeUpdateQueue(CryptoCtrl* ctrl, uint8_t size, uint64_t bus_bytes,
        uint64_t packing_factor, uint64_t tree_height,
        uint64_t tree_node_bytes, AddrRange range_integrity)
        : size(size), bus_bytes(bus_bytes), packing_factor(packing_factor),
          tree_height(tree_height), tree_node_bytes(tree_node_bytes),
          non_leaf_nodes((std::pow(packing_factor, tree_height) - 1) /
              (packing_factor - 1)),
          range_integrity(range_integrity)
    {
    }

    bool is_full() { return queue.size() >= size; }

    std::pair<bool, TreeUpdateRequest> enqueue_request(Addr data_address)
    {
        // NOTE: the latency of address translation can probably be hidden in
        // case packing_factor is not a power of 2, and if it is, the
        // necessary operations can be performed with bit operations
        uint64_t node_id =
            non_leaf_nodes + data_address / bus_bytes / packing_factor;
        uint8_t node_offset = data_address / bus_bytes % packing_factor;
        TreeUpdateRequest new_request = TreeUpdateRequest(data_address);
        for (int i = 0; i < tree_height; i++) {
            // compute actual node address
            Addr node_address =
                range_integrity.start() + node_id * tree_node_bytes;
            DPRINTF(CryptoCtrl,
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

    bool contains_request_node(Addr node_address)
    {
        for (auto& request : queue) {
            if (request.contains_request_node(node_address))
                return true;
        }
        return false;
    };

    bool complete_request_node(Addr node_address)
    {
        auto it = queue.begin();
        while (it != queue.end() && !it->contains_request_node(node_address))
            std::advance(it, 1);
        panic_if(it == queue.end(),
            "could not find element in tree update queue\n");
        it->completed_layers += 1;
        DPRINTF(CryptoCtrl,
            "finished metadata write 0x%x (data address 0x%x)\n",
            node_address, it->data_address);
        if (it->completed_layers >= tree_height) {
            DPRINTF(CryptoCtrl,
                "all metadata writes complete for %s, deleting request "
                "from tree update queue\n",
                it->to_string());
            queue.erase(it);
            return true;
        }
        return false;
    }
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
class CryptoCtrl : public ClockedObject
{
  private:
    System* sys;
    RequestorID requestorId;

    Tick aes_enc_ready, aes_dec_ready;
    uint64_t aes_block_bits, aes_block_bytes;
    uint64_t aes_enc_cycles, aes_dec_cycles;
    uint64_t aes_enc_ii, aes_dec_ii;
    uint64_t counter_bits, counter_bytes;
    uint64_t mac_bits, mac_bytes;
    uint64_t mac_cycles, mac_ii;
    uint64_t packing_factor;
    uint64_t bytes_per_address;

    uint64_t tree_height;
    uint64_t tree_node_bytes;

    uint64_t bus_bytes;

    AddrRange range_total;
    AddrRange range_data;
    AddrRange range_integrity;
    AddrRange range_leaves;

    TreeUpdateQueue treeUpdateQueue;
    bool tree_update_retry_necessary = false;
    std::list<TreeCheckRequest> treeCheckQueue;
    uint64_t tree_check_buffer_size;

    struct PktStats : public Group
    {
        statistics::Scalar reads;
        statistics::Scalar writes;
        statistics::Scalar cpuTotalCountSend;
        statistics::Scalar cpuTotalCountRecv;
        statistics::Scalar memTotalCountSend;
        statistics::Scalar memTotalCountRecv;
        statistics::Scalar mdcacheTotalCountSend;
        statistics::Scalar mdcacheTotalCountRecv;
        statistics::Scalar cpuFailuresCountSend;
        statistics::Scalar cpuRetryCountSend;
        statistics::Scalar memFailuresCountSend;
        statistics::Scalar memRetryCountSend;
        statistics::Scalar mdcacheFailuresCountSend;
        statistics::Scalar mdcacheRetryCountSend;
        PktStats(Group* parent)
            : Group(parent),
              ADD_STAT(reads, statistics::units::Count::get(),
                  "amount of read requests"),
              ADD_STAT(writes, statistics::units::Count::get(),
                  "amount of write requests"),
              ADD_STAT(cpuTotalCountSend, statistics::units::Count::get(),
                  "amount of sent packets"),
              ADD_STAT(cpuTotalCountRecv, statistics::units::Count::get(),
                  "amount of received packets"),
              ADD_STAT(memTotalCountSend, statistics::units::Count::get(),
                  "amount of sent packets"),
              ADD_STAT(memTotalCountRecv, statistics::units::Count::get(),
                  "amount of received packets"),
              ADD_STAT(mdcacheTotalCountSend, statistics::units::Count::get(),
                  "amount of sent packets"),
              ADD_STAT(mdcacheTotalCountRecv, statistics::units::Count::get(),
                  "amount of received packets"),
              ADD_STAT(cpuFailuresCountSend, statistics::units::Count::get(),
                  "amount of response packets which failed to send"),
              ADD_STAT(cpuRetryCountSend, statistics::units::Count::get(),
                  "amount of response packets sent as a result of a retry"),
              ADD_STAT(memFailuresCountSend, statistics::units::Count::get(),
                  "amount of request packets which failed to send"),
              ADD_STAT(memRetryCountSend, statistics::units::Count::get(),
                  "amount of request packets sent as a result of a retry"),
              ADD_STAT(mdcacheFailuresCountSend,
                  statistics::units::Count::get(),
                  "amount of request packets which failed to send"),
              ADD_STAT(mdcacheRetryCountSend, statistics::units::Count::get(),
                  "amount of request packets sent as a result of a retry")
        {
        }
    } stats;

    class MetadataCacheSidePort : public RequestPort
    {
      private:
        CryptoCtrl* owner;

        // store packets for retries
        std::queue<PacketPtr> failedPackets;

      public:
        MetadataCacheSidePort(const std::string& name, CryptoCtrl* owner)
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

    class CPUSidePort : public ResponsePort
    {
      private:
        CryptoCtrl* owner;

        // store packets for retries
        std::queue<PacketPtr> failedPackets;

      public:
        CPUSidePort(const std::string& name, CryptoCtrl* owner)
            : ResponsePort(name), owner(owner)
        {
        }

        // called by the crypto controller
        bool sendPacket(PacketPtr pkt);
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
        /// The object that owns this object (CryptoCtrl)
        CryptoCtrl* owner;

        // store packet for retries
        std::queue<PacketPtr> failedPackets;

      public:
        MemSidePort(const std::string& name, CryptoCtrl* owner)
            : RequestPort(name), owner(owner)
        {
        }

        // called by the cpu side controller to actually transmit packets
        bool sendPacket(PacketPtr pkt);

      protected:
        // called by the mem side controller when responding
        bool recvTimingResp(PacketPtr pkt) override;
        // called by the mem side controller when responding
        void recvReqRetry() override;
        void recvRangeChange() override;
    };

    /**
     * Handle the request from the CPU side
     *
     * @param pkt requesting packet
     * @return true if we can handle the request this cycle, false if the
     *         requestor needs to retry later
     */
    bool handleRequest(PacketPtr pkt);

    /**
     * Handle the respone from the memory side
     *
     * @param pkt responding packet
     * @return true if we can handle the response this cycle, false if the
     *         responder needs to retry later
     */
    bool handleResponse(PacketPtr pkt, ResponseSource source);

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

    // instantiation of the CPU-side ports
    MetadataCacheSidePort mdcachePort;

    // instantiation of the CPU-side ports
    CPUSidePort cpuPort;

    // instantiation of the memory-side port
    MemSidePort memPort;

  public:
    /** constructor
     */
    CryptoCtrl(const CryptoCtrlParams& params);

    /**
     * Create a Packet from given parameters.
     */
    PacketPtr createPkt(Addr addr, size_t size, MemCmd cmd);

    /**
     * Create a Packet copy with a different command.
     */
    PacketPtr createPktFromPkt(PacketPtr pkt, MemCmd cmd);

    /**
     * Change the command of a Packet.
     */
    void modifyPkt(PacketPtr pkt, MemCmd cmd);

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

    /**
     * If data sent from the CPU side needs to be encrypted, emulate
     * encryption feature functionally and also its timing.
     *
     * @param pkt requesting packet
     */
    void CryptoWrite(PacketPtr pkt);

    /**
     * If data sent from the memory side needs to be decrypted, emulate
     * encryption feature functionally and also its timing.
     *
     * @param pkt requesting packet
     */
    void CryptoRead(PacketPtr pkt);
};

} // namespace gem5

#endif // __MEMSEC_CRYPTO_CTRL_HH__
