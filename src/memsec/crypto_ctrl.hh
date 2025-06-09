#ifndef __MEMSEC_CRYPTO_CTRL_HH__
#define __MEMSEC_CRYPTO_CTRL_HH__

#include <cstdint>
#include <queue>

#include "base/statistics.hh"
#include "base/stats/group.hh"
#include "mem/packet.hh"
#include "mem/port.hh"
#include "params/CryptoCtrl.hh"
#include "sim/sim_object.hh"

#define TICK_PER_CYCLE 1000
#define AFTER_1_CYCLE(t) ((t) + TICK_PER_CYCLE)
#define AFTER_N_CYCLES(t, n) ((t) + ((n) * TICK_PER_CYCLE))

// parameters sourced from https://ieeexplore.ieee.org/document/7019004
// AES supports 128 bit blocks
#define AES_BLOCK_BYTES (128 / 8)
// cycle latencies
//#define AES_ENC_CYCLES 336
//#define AES_DEC_CYCLES 216
// initiation intervals
//#define AES_ENC_II 336
//#define AES_DEC_II 216


namespace gem5
{

typedef std::pair<Tick, PacketPtr> DelayedPacket;

/**
 * A very simple controller.
 */
class CryptoCtrl : public SimObject
{
  private:
    uint32_t aes_enc_cycles, aes_dec_cycles;
    uint32_t aes_enc_ii, aes_dec_ii;

    struct PktStats : public Group
    {
        statistics::Scalar reads;
        statistics::Scalar writes;
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
              ADD_STAT(cpuFailuresCountSend, statistics::units::Count::get(),
                  "amount of response packets which failed to sent"),
              ADD_STAT(cpuRetryCountSend, statistics::units::Count::get(),
                  "amount of response packets sent as a result of a retry"),
              ADD_STAT(memFailuresCountSend, statistics::units::Count::get(),
                  "amount of request packets which failed to sent"),
              ADD_STAT(memRetryCountSend, statistics::units::Count::get(),
                  "amount of request packets sent as a result of a retry")
        {
        }
    } stats;

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
         * @param packet the requestor sent
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
        std::queue<PacketPtr> failedPackets;

      public:
        MemSidePort(const std::string& name, CryptoCtrl* owner)
            : RequestPort(name), owner(owner)
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

    /**
     * If data sent from the CPU side needs to be encrypted, emulate
     * encryption feature functionally and also its timing.
     *
     * @param pkt requesting packet
     */
    void AESEncrypt(PacketPtr pkt);

    /**
     * If data sent from the memory side needs to be decrypted, emulate
     * encryption feature functionally and also its timing.
     *
     * @param pkt requesting packet
     */
    void AESDecrypt(PacketPtr pkt);

    /**
     * Creates an entirely new packet.
     *
     * @param address address to fill in
     * @param size packet size to allocate
     * @param flags flags to use
     * @param requestorId id to fill in
     * @param cmd memory command to fill in
     * @return: pointer to created packet
     */
    PacketPtr createPkt(Addr addr, size_t size, uint32_t flags,
        uint16_t requestorId, MemCmd cmd);

    /**
     * Copy existing an existing packet to create a new packet.
     *
     *
     * @param pkt requesting packet
     * @param cmd memory command to fill in
     * @return: pointer to created packet
     */
    PacketPtr createPktFromPkt(PacketPtr pkt, MemCmd cmd);
};

} // namespace gem5

#endif // __MEMSEC_CRYPTO_CTRL_HH__
