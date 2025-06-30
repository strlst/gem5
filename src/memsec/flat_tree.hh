#ifndef __MEMSEC_FLAT_TREE_HH__
#define __MEMSEC_FLAT_TREE_HH__

#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

template<typename T, const T default_value> class FlatTree
{
  public:
    FlatTree(uint64_t tree_height)
        : tree_height(tree_height), max_value(std::exp2(tree_height))
    {
        tree.resize(max_value);
        std::fill(tree.begin(), tree.end(), default_value);
    }

    void insert(uint64_t address, T data)
    {
        assert(address < max_value);
        tree.at(address) = data;
    }

    T lookup(uint64_t address)
    {
        assert(address < max_value);
        return tree.at(address);
    }

    uint64_t root_address() { return 1; }

    uint64_t left_child_address(uint64_t address) { return address << 1; }

    uint64_t right_child_address(uint64_t address)
    {
        return address << 1 | 1;
    }

    uint64_t parent_address(uint64_t address) { return address >> 1; }

  private:
    uint64_t tree_height;
    std::vector<T> tree;
    uint64_t max_value;
};

#endif // __MEMSEC_FLAT_TREE_HH__
