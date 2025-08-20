#ifndef __MEMSEC_CRYPTO_EVENT_HH__
#define __MEMSEC_CRYPTO_EVENT_HH__

#include "memsec/crypto_ctrl.hh"
#include "memsec/int_tree.hh"

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

class DataMACEvent : public Event
{
  private:
    CryptoCtrl *ctrl;
    PacketPtr pkt;
    DataMACEventType type;
  public:
    DataMACEvent(CryptoCtrl *ctrl, PacketPtr pkt, DataMACEventType type)
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
        }
    }
};

class IntegrityMACEvent : public Event
{
  private:
    IntTRB *int_trb;
    PacketPtr pkt;
    IntegrityMACEventType type;
  public:
    IntegrityMACEvent(IntTRB *int_trb, PacketPtr pkt,
        IntegrityMACEventType type)
        : Event(Default_Pri, AutoDelete), int_trb(int_trb), pkt(pkt),
          type(type)
    {
    }

    void process() override
    {
        // process packet by using callback
        switch (type) {
        case IntegrityMACCheck:
            int_trb->IntegrityMACCheck(pkt);
            break;
        case IntegrityMACUpdate:
            int_trb->IntegrityMACUpdate(pkt);
            break;
        }
    }
};

}

#endif // __MEMSEC_CRYPTO_EVENT_HH__
