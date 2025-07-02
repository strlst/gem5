#ifndef __MEMSEC_FLAT_TREE_HH__
#define __MEMSEC_FLAT_TREE_HH__

#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

#include "mem/packet.hh"

namespace gem5
{

/**
 * FlatTree is a simple tree that stores values in a flat array.
 * It is used to store integrity values for a given address range.
 * The node lookups are implemented in a way which can be efficiently done
 * using simple bit operations, making it suitable for hardware
 * implementation, while at the same time enabling parallel lookups.
 * This is because each parent address of each level can be easily inferred
 * from the leaf address.
 *
 * These parallel lookups would not help in situations in which nodes of the
 * tree must be updated sequentially, such as during write chains of the tree
 * where each node must be computed before the parent node can be computed.
 * However, for read chains of the tree, parallel lookups are possible.
 *
 * @tparam T the type of the values stored in the tree
 * @tparam default_value the default value for the tree nodes
 */
template<typename T, const T default_value> class FlatTree
{
  public:
    FlatTree(const uint64_t tree_height, const uint64_t packing_factor)
        : tree_height(tree_height), packing_factor(packing_factor)
    {
        // we use the known formula n = (p^(h + 1) - 1) / (p - 1) for the
        // node count, while subtracting the root node
        max_address =
            uint64_t((std::pow(packing_factor, tree_height + 1) - 1) /
                (packing_factor - 1)) -
            1;
        tree.resize(max_address);
        std::fill(tree.begin(), tree.end(), default_value);
    }

    void update(Addr address, T data)
    {
        assert(address < max_address);
        tree.at(address) = data;
    }

    T lookup(Addr address)
    {
        assert(address < max_address);
        return tree.at(address);
    }

    Addr child_address(Addr address, uint64_t child)
    {
        assert(child < packing_factor);
        return (address << packing_factor) | child;
    }

    Addr parent_address(Addr address) { return address >> packing_factor; }

    Addr get_root_address() { return 0; }

    Addr get_max_address() { return max_address; }

  private:
    std::vector<T> tree;
    uint64_t tree_height;
    uint64_t packing_factor;
    Addr max_address;
};

} // namespace gem5

#endif // __MEMSEC_FLAT_TREE_HH__
