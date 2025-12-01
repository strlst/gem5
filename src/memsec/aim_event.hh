#ifndef __MEMSEC_AIM_EVENT_HH__
#define __MEMSEC_AIM_EVENT_HH__

#include "memsec/aim_ctrl.hh"
#include "memsec/int_tree.hh"

namespace gem5
{

class AIMWriteEvent : public Event
{
  private:
    AIMCtrl *ctrl;
    PacketPtr pkt;
    std::vector<PacketId> data_ids;
  public:
    AIMWriteEvent(AIMCtrl *ctrl, PacketPtr pkt, std::vector<PacketId> data_ids)
        : Event(Default_Pri, AutoDelete), ctrl(ctrl), pkt(pkt),
          data_ids(data_ids)
    {
    }

    void process() override
    {
        // process packet by using callback
        ctrl->opAIMWrite(pkt, data_ids);
    }
};

class AIMReadEvent : public Event
{
  private:
    AIMCtrl *ctrl;
    PacketPtr pkt;
    std::vector<PacketId> data_ids;
  public:
    AIMReadEvent(AIMCtrl *ctrl, PacketPtr pkt, std::vector<PacketId> data_ids)
        : Event(Default_Pri, AutoDelete), ctrl(ctrl), pkt(pkt),
          data_ids(data_ids)
    {
    }

    void process() override
    {
        // process packet by using callback
        ctrl->opAIMRead(pkt, data_ids);
    };
};

class DataMACEvent : public Event
{
  private:
    AIMCtrl *ctrl;
    PacketPtr pkt;
    DataMACEventType type;
  public:
    DataMACEvent(AIMCtrl *ctrl, PacketPtr pkt, DataMACEventType type)
        : Event(Default_Pri, AutoDelete), ctrl(ctrl), pkt(pkt), type(type)
    {
    }

    void process() override
    {
        // process packet by using callback
        switch (type) {
        case DataMACCheck:
            ctrl->opDataMACCheck(pkt);
            break;
        case DataMACUpdate:
            ctrl->opDataMACUpdate(pkt);
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

class MDCacheSendEvent : public Event
{
  private:
    IntTRB *int_trb;
    PacketPtr pkt;
  public:
    MDCacheSendEvent(IntTRB *int_trb, PacketPtr pkt)
        : Event(Default_Pri, AutoDelete), int_trb(int_trb), pkt(pkt)
    {
    }

    void process() override
    {
        // process packet by using callback
        int_trb->MDCacheSend(pkt);
    }
};

}

#endif // __MEMSEC_AIM_EVENT_HH__
