#ifndef __MEMSEC_FLAT_TREE_HH__
#define __MEMSEC_FLAT_TREE_HH__

#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

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
        : tree_height(tree_height), packing_factor(packing_factor),
          max_address(std::pow(packing_factor, tree_height))
    {
        // we need to assert that packing_factor is a power of two
        // http://graphics.stanford.edu/~seander/bithacks.html
        assert((packing_factor > 1) &
            !(packing_factor & (packing_factor - 1)));
        tree.resize(max_address);
        std::fill(tree.begin(), tree.end(), default_value);
    }

    void insert(Addr address, T data)
    {
        assert(address < max_address);
        tree.at(address) = data;
    }

    T lookup(Addr address)
    {
        assert(address < max_address);
        return tree.at(address);
    }

    Addr address_as_leaf_node(Addr address)
    {
        // due to the layout of the flat tree, all leaf addresses have the
        // uppermost bit set, so we can address leaves in the tree by setting
        // the uppermost bit on the addresses
        return (address | (1 << (tree_height - 1)));
    }

    Addr child_address(Addr address, uint64_t child)
    {
        assert(child < packing_factor);
        return (address << packing_factor) | child;
    }

    Addr parent_address(Addr address) { return address >> packing_factor; }

    Addr get_root_address() { return 1; }

    Addr get_max_address() { return max_address; }

  private:
    std::vector<T> tree;
    uint64_t tree_height;
    uint64_t packing_factor;
    Addr max_address;
};

} // namespace gem5

#endif // __MEMSEC_FLAT_TREE_HH__
