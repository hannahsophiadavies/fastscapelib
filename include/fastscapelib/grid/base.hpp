/**
 * Common grid element types (node status, neighbors).
 * grid base (abstract) class.
 */
#ifndef FASTSCAPELIB_GRID_BASE_H
#define FASTSCAPELIB_GRID_BASE_H

#include <cstddef>
#include <cstdint>
#include <map>

#include "xtensor/xadapt.hpp"
#include "xtensor/xtensor.hpp"
#include "xtensor/xview.hpp"

#include "fastscapelib/utils/iterators.hpp"
#include "fastscapelib/utils/xtensor_utils.hpp"


namespace fastscapelib
{

    //*****************
    //* Grid boundaries
    //*****************

    /**
     * Status at a grid/mesh node.
     */
    enum class node_status : std::uint8_t
    {
        core = 0,
        fixed_value_boundary = 1,
        fixed_gradient_boundary = 2,
        looped_boundary = 3
    };

    namespace detail
    {

        template <class V>
        class node_status_filter
        {
        public:
            node_status_filter(V ref)
                : m_ref(ref)
            {
            }

            template <class G>
            inline bool operator()(const G& grid, typename G::size_type idx) const
            {
                return grid.status_at_nodes()[idx] == m_ref;
            }

        private:
            V m_ref;
        };

        template <class V>
        auto make_node_status_filter(V value)
        {
            return node_status_filter<V>(value);
        }

        inline bool node_status_cmp(node_status a, node_status b)
        {
            static std::map<node_status, int> priority{ { node_status::core, 0 },
                                                        { node_status::looped_boundary, 1 },
                                                        { node_status::fixed_gradient_boundary, 2 },
                                                        { node_status::fixed_value_boundary, 3 } };

            return priority[a] < priority[b];
        }

    }  // namespace detail


    /**
     * Status at grid boundary nodes.
     */
    class boundary_status
    {
    protected:
        bool is_looped(node_status status) const;
    };

    inline bool boundary_status::is_looped(const node_status status) const
    {
        return status == node_status::looped_boundary;
    }


    //***************
    //* Grid elements
    //***************

    /**
     * Grid/mesh node.
     */
    struct node
    {
        std::size_t idx;    /**< Node index */
        node_status status; /**< Node status */
    };

    /**
     * Neighbor node.
     */
    struct neighbor
    {
        std::size_t idx;    /**< Index of the neighbor node */
        double distance;    /**< Distance to the neighbor node */
        node_status status; /**< Status at the neighbor node */

        bool operator==(const neighbor& rhs) const
        {
            return (idx == rhs.idx) && (distance == rhs.distance) && (status == rhs.status);
        }
    };

    namespace detail
    {
        /**
         * Add a given offset (positive, negative or zero) to a grid node index.
         *
         * This utility function is mainly to avoid signed/unsigned conversion
         * warnings. Use it carefully.
         */
        inline std::size_t add_offset(std::size_t idx, std::ptrdiff_t offset)
        {
            return static_cast<std::size_t>(static_cast<std::ptrdiff_t>(idx) + offset);
        }
    }

    /**********************************
     * Grid neighbors indices caching *
     **********************************/

    /**
     * Provides a cache for neighbors indices queries of a particular node of the grid.
     */
    template <std::uint8_t N>
    class neighbors_cache
    {
    public:
        static constexpr unsigned int cache_width = N;
        using neighbors_indices_type = std::array<std::size_t, N>;

        neighbors_cache(std::size_t size)
            : m_cache(cache_shape_type({ size }))
        {
            for (std::size_t i = 0; i < size; ++i)
            {
                m_cache[i].fill(std::numeric_limits<std::size_t>::max());
            }
        }

        bool has(const std::size_t& idx) const
        {
            return m_cache[idx][0] == std::numeric_limits<std::size_t>::max() ? false : true;
        }

        neighbors_indices_type& get(const std::size_t& idx)
        {
            return m_cache[idx];
        }

        neighbors_indices_type& get_storage(const std::size_t& idx)
        {
            return m_cache[idx];
        }

        void store(const std::size_t& idx, const neighbors_indices_type& neighbors_indices)
        {
            m_cache[idx] = neighbors_indices;
        }

        std::size_t cache_size() const
        {
            return m_cache.size();
        };

        std::size_t cache_used() const
        {
            std::size_t count = 0;

            for (std::size_t i = 0; i < m_cache.size(); ++i)
            {
                if (m_cache[i][0] != std::numeric_limits<std::size_t>::max())
                {
                    count += 1;
                }
            }

            return count;
        };

        void reset()
        {
            for (std::size_t i = 0; i < m_cache.size(); ++i)
            {
                m_cache[i].fill(std::numeric_limits<std::size_t>::max());
            }
        }

        void remove(const std::size_t& idx)
        {
            m_cache[idx].fill(std::numeric_limits<std::size_t>::max());
        }

    protected:
        using cache_type = xt::xtensor<neighbors_indices_type, 1>;
        using cache_shape_type = typename cache_type::shape_type;

        cache_type m_cache;
    };


    /**
     * A pass-through/no cache for neighbors indices queries of a particular node of the grid.
     */
    template <unsigned int N>
    class neighbors_no_cache
    {
    public:
        static constexpr unsigned int cache_width = N;
        using neighbors_indices_type = std::array<std::size_t, N>;

        neighbors_no_cache(std::size_t /*size*/)
        {
        }

        bool has(const std::size_t& /*idx*/) const
        {
            return false;
        }

        neighbors_indices_type& get(const std::size_t& /*idx*/)
        {
            return m_node_neighbors;
        }

        neighbors_indices_type& get_storage(const std::size_t& /*idx*/)
        {
            return m_node_neighbors;
        }

        void store(const std::size_t& /*idx*/, const neighbors_indices_type neighbors_indices)
        {
            m_node_neighbors = neighbors_indices;
        }

        std::size_t cache_size() const
        {
            return 0;
        }

        std::size_t cache_used() const
        {
            return 0;
        }

        void reset()
        {
        }

        void remove(const std::size_t& /*idx*/)
        {
        }

    protected:
        neighbors_indices_type m_node_neighbors;
    };

    //****************
    //* Grid interface
    //****************

    template <class G>
    struct grid_inner_types;

    /**
     * @class grid
     * @brief Add a common interface to all grid/mesh types.
     *
     * This class only defines a basic interface for all grid/mesh types.
     * It does not embed any data member, this responsibility
     * is delegated to the inheriting classes.
     *
     * @tparam G Derived grid type.
     */
    template <class G>
    class grid
    {
    public:
        using derived_grid_type = G;
        using inner_types = grid_inner_types<derived_grid_type>;

        static constexpr bool is_structured()
        {
            return inner_types::is_structured;
        }

        static constexpr bool is_uniform()
        {
            return inner_types::is_uniform;
        }

        using grid_data_type = typename inner_types::grid_data_type;

        using xt_selector = typename inner_types::xt_selector;

        static constexpr std::size_t xt_ndims()
        {
            return inner_types::xt_ndims;
        }

        using xt_type = xt_tensor_t<xt_selector, grid_data_type, xt_ndims()>;

        using size_type = typename xt_type::size_type;
        using shape_type = typename xt_type::shape_type;

        using neighbors_cache_type = typename inner_types::neighbors_cache_type;

        static constexpr std::uint8_t n_neighbors_max()
        {
            return inner_types::n_neighbors_max;
        }

        static_assert(neighbors_cache_type::cache_width == 0
                          || neighbors_cache_type::cache_width >= n_neighbors_max(),
                      "Cache width is too small!");

        using neighbors_type = std::vector<neighbor>;
        using neighbors_count_type = typename inner_types::neighbors_count_type;
        // using xt:xtensor for indices as not all containers support resizing
        // (e.g., using pyarray may cause segmentation faults with Python)
        using neighbors_indices_type = xt::xtensor<size_type, 1>;
        using neighbors_distances_type = xt_tensor_t<xt_selector, grid_data_type, 1>;

        using node_status_type = xt_tensor_t<xt_selector, node_status, inner_types::xt_ndims>;

        size_type size() const noexcept;

        shape_type shape() const noexcept;

        const node_status_type& status_at_nodes() const;

        grid_data_type node_area(const size_type& idx) const noexcept;

        const neighbors_count_type& neighbors_count(const size_type& idx) const;

        neighbors_distances_type neighbors_distances(const size_type& idx) const;

        // no const since it may update the cache internally
        neighbors_indices_type neighbors_indices(const size_type& idx);

        neighbors_indices_type& neighbors_indices(const size_type& idx,
                                                  neighbors_indices_type& neighbors_indices);

        neighbors_type neighbors(const size_type& idx);

        neighbors_type& neighbors(const size_type& idx, neighbors_type& neighbors);

        inline filtered_index_iterator<grid, detail::node_status_filter<node_status>>
        nodes_indices_begin(node_status status)
        {
            return filtered_index_iterator<grid, detail::node_status_filter<node_status>>(
                *this, detail::make_node_status_filter(status), 0);
        }

        inline filtered_index_iterator<grid, detail::node_status_filter<node_status>>
        nodes_indices_end(node_status status)
        {
            return filtered_index_iterator<grid, detail::node_status_filter<node_status>>(
                *this, detail::make_node_status_filter(status), size());
        }

        inline index_iterator<grid> nodes_indices_begin()
        {
            return index_iterator<grid>(*this, 0);
        }

        inline index_iterator<grid> nodes_indices_end()
        {
            return index_iterator<grid>(*this, size());
        }

        inline std::reverse_iterator<index_iterator<grid>> nodes_indices_rbegin()
        {
            return std::reverse_iterator<index_iterator<grid>>(nodes_indices_end());
        }

        inline std::reverse_iterator<index_iterator<grid>> nodes_indices_rend()
        {
            return std::reverse_iterator<index_iterator<grid>>(nodes_indices_begin());
        }

        inline detail::node_indices_iterator<grid> nodes_indices()
        {
            return *this;
        };

        const neighbors_cache_type& neighbors_indices_cache()
        {
            return m_neighbors_indices_cache;
        };

    protected:
        using neighbors_indices_impl_type = typename neighbors_cache_type::neighbors_indices_type;
        using neighbors_distances_impl_type =
            typename std::array<grid_data_type, inner_types::n_neighbors_max>;

        grid(std::size_t size)
            : m_neighbors_indices_cache(neighbors_cache_type(size)){};
        ~grid() = default;

        const derived_grid_type& derived_grid() const noexcept;
        derived_grid_type& derived_grid() noexcept;

        void neighbors_indices_impl(neighbors_indices_impl_type& neighbors,
                                    const size_type& idx) const;

        inline const neighbors_indices_impl_type& neighbors_indices_cache(const size_type& idx);

        inline const neighbors_distances_impl_type& neighbors_distances_impl(
            const size_type& idx) const;

        neighbors_cache_type m_neighbors_indices_cache;
    };


    template <class G>
    inline auto grid<G>::derived_grid() const noexcept -> const derived_grid_type&
    {
        return *static_cast<const derived_grid_type*>(this);
    }

    template <class G>
    inline auto grid<G>::derived_grid() noexcept -> derived_grid_type&
    {
        return *static_cast<derived_grid_type*>(this);
    }

    /**
     * @name Grid properties
     */
    //@{
    /**
     * Returns the total number of grid nodes.
     */
    template <class G>
    inline auto grid<G>::size() const noexcept -> size_type
    {
        return derived_grid().m_size;
    }

    /**
     * Returns the total number of grid nodes.
     */
    template <class G>
    inline auto grid<G>::shape() const noexcept -> shape_type
    {
        return derived_grid().m_shape;
    }

    /**
     * Returns a constant reference to the array of status at grid nodes.
     */
    template <class G>
    inline auto grid<G>::status_at_nodes() const -> const node_status_type&
    {
        return derived_grid().m_status_at_nodes;
    }

    /**
     * Returns the drainage at grid node.
     */
    template <class G>
    inline auto grid<G>::node_area(const size_type& /*idx*/) const noexcept -> grid_data_type
    {
        return derived_grid().m_node_area;
    }

    /**
     * Returns a constant reference to the neighbors count of a given grid node.
     *
     * @param idx Index of the grid node.
     */
    template <class G>
    inline auto grid<G>::neighbors_count(const size_type& idx) const -> const neighbors_count_type&
    {
        return derived_grid().neighbors_count(idx);
    }

    template <class G>
    inline auto grid<G>::neighbors_indices(const size_type& idx) -> neighbors_indices_type
    {
        neighbors_indices_type indices = xt::adapt(neighbors_indices_cache(idx));
        auto view = xt::view(indices, xt::range(0, neighbors_count(idx)));

        return view;
    }

    template <class G>
    inline auto grid<G>::neighbors_distances(const size_type& idx) const -> neighbors_distances_type
    {
        neighbors_distances_type distances = xt::adapt(neighbors_distances_impl(idx));
        auto view = xt::view(distances, xt::range(0, neighbors_count(idx)));

        return view;
    }

    /**
     * @name Iterators
     */
    /**
     * Iterate over the neighbors of a given grid node.
     *
     * Follows looped boundary conditions, if any.
     *
     * @param idx Index of the grid node.
     * @return The vector of the neighbors of that grid node.
     */
    template <class G>
    inline auto grid<G>::neighbors(const size_type& idx) -> neighbors_type
    {
        neighbors_type nb;
        neighbors(idx, nb);

        return nb;
    }

    /**
     * Iterate over the neighbors indices of a given grid node.
     *
     * Follows looped boundary conditions, if any.
     *
     * @param idx Index of the grid node.
     * @param neighbors_indices Reference to the vector to be filled with the neighbors
     *                          indices of that grid node.
     */
    template <class G>
    inline auto grid<G>::neighbors_indices(const size_type& idx,
                                           neighbors_indices_type& neighbors_indices)
        -> neighbors_indices_type&
    {
        const auto& n_count = neighbors_count(idx);
        const auto& n_indices = neighbors_indices_cache(idx);

        if (neighbors_indices.size() != n_count)
        {
            neighbors_indices.resize({ n_count });
        }

        for (neighbors_count_type i = 0; i < n_count; ++i)
        {
            neighbors_indices[i] = n_indices[i];
        }

        return neighbors_indices;
    }

    /**
     * Iterate over the neighbors of a given grid node.
     *
     * Follows looped boundary conditions, if any.
     *
     * @param idx Index of the grid node.
     * @param neighbors Reference to the vector to be filled with the neighbors of that grid node.
     */
    template <class G>
    inline auto grid<G>::neighbors(const size_type& idx, neighbors_type& neighbors)
        -> neighbors_type&
    {
        size_type n_idx;
        const auto& n_count = neighbors_count(idx);
        const auto& n_indices = neighbors_indices_cache(idx);
        const auto& n_distances = neighbors_distances_impl(idx);

        if (neighbors.size() != n_count)
        {
            neighbors.resize({ n_count });
        }

        for (neighbors_count_type i = 0; i < n_count; ++i)
        {
            n_idx = n_indices[i];
            neighbors[i] = neighbor({ n_idx, n_distances[i], status_at_nodes()[n_idx] });
        }

        return neighbors;
    }
    //@}

    /**
     *
     *
     * @param idx Index of the grid node.
     * @return Reference to the array of the neighbors indices of that grid node.
     */
    template <class G>
    inline auto grid<G>::neighbors_indices_cache(const size_type& idx)
        -> const neighbors_indices_impl_type&
    {
        if (m_neighbors_indices_cache.has(idx))
        {
            neighbors_indices_impl_type& n_indices = m_neighbors_indices_cache.get(idx);
            return n_indices;
        }
        else
        {
            neighbors_indices_impl_type& n_indices = m_neighbors_indices_cache.get_storage(idx);
            this->derived_grid().neighbors_indices_impl(n_indices, idx);
            return n_indices;
        }
    }

    template <class G>
    inline auto grid<G>::neighbors_indices_impl(neighbors_indices_impl_type& neighbors,
                                                const size_type& idx) const -> void
    {
        return derived_grid().neighbors_indices_impl(neighbors, idx);
    }

    template <class G>
    inline auto grid<G>::neighbors_distances_impl(const size_type& idx) const
        -> const neighbors_distances_impl_type&
    {
        return derived_grid().neighbors_distances_impl(idx);
    }

}

#endif  // FASTSCAPELIB_GRID_BASE_H
