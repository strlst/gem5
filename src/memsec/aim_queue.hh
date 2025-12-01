#ifndef __MEMSEC_AIM_QUEUE_HH__
#define __MEMSEC_AIM_QUEUE_HH__

#include "mem/packet.hh"

namespace gem5
{

struct AIMQueueNode
{
    PacketPtr data_pkt;
    PacketId data_id;
    bool data_op_complete = false;
    bool counter_read_complete = false;

    AIMQueueNode() : data_pkt(0), data_id(0) {}

    AIMQueueNode(PacketPtr data_pkt, PacketId data_id)
        : data_pkt(data_pkt), data_id(data_id)
    {
    }

    inline bool is_complete()
    {
        return data_op_complete && counter_read_complete;
    }

    bool complete_data_op()
    {
        data_op_complete = true;
        return is_complete();
    }

    bool complete_counter_read()
    {
        counter_read_complete = true;
        return is_complete();
    }
};

class AIMQueue
{
  private:
    std::unordered_map<PacketId, AIMQueueNode> requests;

  public:
    AIMQueue() {}

    void add(PacketPtr data_pkt, PacketId data_id)
    {
        requests[data_id] = AIMQueueNode(data_pkt, data_id);
    }

    PacketPtr get_data_pkt(PacketId data_id)
    {
        return requests.at(data_id).data_pkt;
    }

    bool complete_data_op(PacketId data_id)
    {
        return requests.at(data_id).complete_data_op();
    }

    bool complete_counter_read(PacketId data_id)
    {
        return requests.at(data_id).complete_counter_read();
    }

    void erase_data_read(PacketId data_id) { requests.erase(data_id); }
};

}

#endif // __MEMSEC_AIM_QUEUE_HH__
