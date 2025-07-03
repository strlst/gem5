#ifndef __MEMSEC_BMT_NODE_HH__
#define __MEMSEC_BMT_NODE_HH__

#include <cstdint>
#include <vector>

struct BMTNode
{
    std::vector<uint64_t> counters;
    uint64_t mac;

    BMTNode() {}

    inline void init(uint64_t packing_factor)
    {
        counters = std::vector<uint64_t>(packing_factor, 0);
        mac = 0;
    }

    inline void increment(uint64_t position) { counters.at(position)++; }

    inline void update_mac(uint64_t parent_counter)
    {
        for (uint64_t i = 0; i < counters.size(); i++) {
            // NOTE: this should at least emulate the complex
            // MAC calculation process
            mac ^= mac + counters.at(i);
        }
        mac ^= mac + parent_counter;
    }

    inline std::string to_string()
    {
        std::string counters_str = "";
        for (uint64_t i = 0; i < counters.size(); i++) {
            counters_str += std::to_string(counters.at(i)) + " ";
        }
        return "bmt_node(counters=" + counters_str +
            " mac=" + std::to_string(mac) + ")";
    }
};

#endif // __MEMSEC_BMT_NODE_HH__
