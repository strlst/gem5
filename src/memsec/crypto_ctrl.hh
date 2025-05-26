#ifndef __MEMSEC_CRYPTO_CTRL_HH__
#define __MEMSEC_CRYPTO_CTRL_HH__

#include "base/statistics.hh"
#include "base/stats/group.hh"
#include "mem/packet.hh"
#include "mem/port.hh"
#include "params/CryptoCtrl.hh"
#include "sim/sim_object.hh"

namespace gem5
{

/**
 * A very simple controller.
 */
class CryptoCtrl : public SimObject
{
  private:
    struct PktStats : public Group
    {
        statistics::Scalar reads;
        statistics::Scalar writes;
        statistics::Scalar cpuTotalCountSend;
        statistics::Scalar cpuTotalCountRecv;
        statistics::Scalar memTotalCountSend;
        statistics::Scalar memTotalCountRecv;
        statistics::Scalar memFailuresCountSend;
        statistics::Scalar memRetryCountSend;
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
              ADD_STAT(memFailuresCountSend, statistics::units::Count::get(),
                  "amount of packets which failed to sent"),
              ADD_STAT(memRetryCountSend, statistics::units::Count::get(),
                  "amount of packets sent as a result of a retry")
        {
        }
    } stats;

    class CPUSidePort : public ResponsePort
    {
      private:
        CryptoCtrl* owner;

      public:
        CPUSidePort(const std::string& name, CryptoCtrl* owner)
            : ResponsePort(name), owner(owner)
        {
        }

        void sendPacket(PacketPtr pkt);
        AddrRangeList getAddrRanges() const override;

      protected:
        Tick recvAtomic(PacketPtr pkt) override
        {
            panic("recvAtomic unimpl.");
        }

        /**
         * Receive a functional request packet from the request port.
         * Performs a "debug" access updating/reading the data in place.
         *
         * @param packet the requestor sent.
         */
        void recvFunctional(PacketPtr pkt) override;

        /**
         * Receive a timing request from the request port.
         *
         * @param the packet that the requestor sent
         * @return whether this object can consume the packet. If false, we
         *         will call sendRetry() when we can try to receive this
         *         request again.
         */
        bool recvTimingReq(PacketPtr pkt) override;

        /**
         * Called by the request port if sendTimingResp was called on this
         * response port (causing recvTimingResp to be called on the request
         * port) and was unsuccesful.
         */
        void recvRespRetry() override;
    };

    class MemSidePort : public RequestPort
    {
      private:
        /// The object that owns this object (CryptoCtrl)
        CryptoCtrl* owner;

        // store packet for retries
        PacketPtr failedPkt;

      public:
        MemSidePort(const std::string& name, CryptoCtrl* owner)
            : RequestPort(name), owner(owner), failedPkt(nullptr)
        {
        }

        void sendPacket(PacketPtr pkt);

      protected:
        /**
         * Receive a timing response from the response port.
         */
        bool recvTimingResp(PacketPtr pkt) override;

        /**
         * Called by the response port if sendTimingReq was called on this
         * request port (causing recvTimingReq to be called on the responder
         * port) and was unsuccesful.
         */
        void recvReqRetry() override;

        /**
         * Called to receive an address range change from the peer responder
         * port. The default implementation ignores the change and does
         * nothing. Override this function in a derived class if the owner
         * needs to be aware of the address ranges, e.g. in an
         * interconnect component like a bus.
         */
        void recvRangeChange() override;
    };

    /**
     * Handle the request from the CPU side
     *
     * @param requesting packet
     * @return true if we can handle the request this cycle, false if the
     *         requestor needs to retry later
     */
    bool handleRequest(PacketPtr pkt);

    /**
     * Handle the respone from the memory side
     *
     * @param responding packet
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

    /**
     * Creates an entirely new packet.
     *
     * @return: pointer to created packet
     */
    PacketPtr createPkt(Addr addr, size_t size, uint32_t flags,
        uint16_t requestorId, MemCmd cmd);

    /**
     * Copy existing an existing packet to create a new packet.
     *
     * @return: pointer to created packet
     */
    PacketPtr createPktFromPkt(PacketPtr pkt, MemCmd cmd);

    /// Instantiation of the CPU-side ports
    CPUSidePort cpuPort;

    /// Instantiation of the memory-side port
    MemSidePort memPort;

  public:
    /** constructor
     */
    CryptoCtrl(const CryptoCtrlParams& params);

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
};

} // namespace gem5

#endif // __MEMSEC_CRYPTO_CTRL_HH__
