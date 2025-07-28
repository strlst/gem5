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

class MACEvent : public Event
{
  private:
    CryptoCtrl *ctrl;
    PacketPtr pkt;
    MACEventType type;
  public:
    MACEvent(CryptoCtrl *ctrl, PacketPtr pkt, MACEventType type)
        : Event(Default_Pri, AutoDelete), ctrl(ctrl), pkt(pkt), type(type)
    {
    }

    void process() override
    {
        // process packet by using callback
        switch (type) {
            case DataMACCheck:
                ctrl->DataMACCheck(pkt);
                break;
            case DataMACUpdate:
                ctrl->DataMACUpdate(pkt);
                break;
            case IntegrityMACCheck:
                ctrl->IntegrityMACCheck(pkt);
                break;
            case IntegrityMACUpdate:
                ctrl->IntegrityMACUpdate(pkt);
                break;
        }
    }
};

}

#endif // __MEMSEC_CRYPTO_EVENT_HH__
