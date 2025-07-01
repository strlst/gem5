#ifndef __MEMSEC_CRYPTO_EVENT_HH__
#define __MEMSEC_CRYPTO_EVENT_HH__

#include "crypto_ctrl.hh"

namespace gem5
{

class CryptoWriteEvent : public Event
{
  private:
    CryptoCtrl *ctrl;
    PacketPtr pkt;
  public:
    CryptoWriteEvent(CryptoCtrl *ctrl, PacketPtr pkt)
        : Event(Default_Pri, AutoDelete), ctrl(ctrl), pkt(pkt)
    {
    }
    void process() override
    {
        // process packet by using callback
        ctrl->CryptoWrite(pkt);
    }
};

class CryptoReadEvent : public Event
{
  private:
    CryptoCtrl *ctrl;
    PacketPtr pkt;
  public:
    CryptoReadEvent(CryptoCtrl *ctrl, PacketPtr pkt)
        : Event(Default_Pri, AutoDelete), ctrl(ctrl), pkt(pkt)
    {
    }
    void process() override
    {
        // process packet by using callback
        ctrl->CryptoRead(pkt);
    };

};

}

#endif // __MEMSEC_CRYPTO_EVENT_HH__
