//  Copyright (c) 2017-2019 Hartmut Kaiser
//  Copyright (c) 2017 Parsa Amini
//  Copyright (c) 2019 Bita Hasheminezhad
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#if !defined(PHYLANX_DIST_CANNON_PRODUCT_IMPL_OCT_17_2019_0322PM)
#define PHYLANX_DIST_CANNON_PRODUCT_IMPL_OCT_17_2019_0322PM

#include <phylanx/config.hpp>
#include <phylanx/execution_tree/annotation.hpp>
#include <phylanx/execution_tree/localities_annotation.hpp>
#include <phylanx/execution_tree/locality_annotation.hpp>
#include <phylanx/execution_tree/meta_annotation.hpp>
#include <phylanx/execution_tree/primitives/node_data_helpers.hpp>
#include <phylanx/ir/node_data.hpp>
#include <phylanx/plugins/common/dot_operation_nd.hpp>
#include <phylanx/plugins/dist_matrixops/dist_cannon_product.hpp>
#include <phylanx/util/distributed_matrix.hpp>
#include <phylanx/util/distributed_vector.hpp>

#include <hpx/assertion.hpp>
#include <hpx/collectives/all_reduce.hpp>
#include <hpx/errors/throw_exception.hpp>
#include <hpx/include/lcos.hpp>
#include <hpx/include/naming.hpp>
#include <hpx/include/util.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <blaze/Math.h>
#include <blaze/math/DynamicMatrix.h>
#include <blaze_tensor/Math.h>

using std_int64_t = std::int64_t;
using std_uint8_t = std::uint8_t;

////////////////////////////////////////////////////////////////////////////////
REGISTER_DISTRIBUTED_VECTOR_DECLARATION(double);
REGISTER_DISTRIBUTED_VECTOR_DECLARATION(std_int64_t);
REGISTER_DISTRIBUTED_VECTOR_DECLARATION(std_uint8_t);

HPX_REGISTER_ALLREDUCE_DECLARATION(double);
HPX_REGISTER_ALLREDUCE_DECLARATION(std_int64_t);
HPX_REGISTER_ALLREDUCE_DECLARATION(std_uint8_t);

using blaze_vector_double = blaze::DynamicVector<double>;
using blaze_vector_std_int64_t = blaze::DynamicVector<std::int64_t>;
using blaze_vector_std_uint8_t = blaze::DynamicVector<std::uint8_t>;

HPX_REGISTER_ALLREDUCE_DECLARATION(blaze_vector_double);
HPX_REGISTER_ALLREDUCE_DECLARATION(blaze_vector_std_int64_t);
HPX_REGISTER_ALLREDUCE_DECLARATION(blaze_vector_std_uint8_t);

////////////////////////////////////////////////////////////////////////////////
namespace phylanx { namespace dist_matrixops { namespace primitives {
    ///////////////////////////////////////////////////////////////////////////
    template <typename T>
    execution_tree::primitive_argument_type dist_cannon_product::product(
        ir::node_data<T>&& lhs, ir::node_data<T>&& rhs,
        execution_tree::localities_information&& lhs_localities,
        execution_tree::localities_information const& rhs_localities) const
    {
        // What if RHS or LHS isn't distributed?
        std::size_t lhs_num_cols = lhs_localities.columns();
        std::size_t rhs_num_rows = rhs_localities.rows();
        execution_tree::tiling_span const& lhs_col_span =
            lhs_localities.get_span(0);
        execution_tree::tiling_span const& lhs_row_span =
            lhs_localities.get_span(1);

        execution_tree::tiling_span const& rhs_col_span =
            rhs_localities.get_span(0);
        execution_tree::tiling_span const& rhs_row_span =
            rhs_localities.get_span(1);

        // Maybe this error should be split to be more descriptive
        if (lhs_num_cols % lhs_col_span.size() != 0 ||
            rhs_num_rows % rhs_row_span.size() != 0)
        {
            HPX_THROW_EXCEPTION(hpx::bad_parameter,
                "dist_cannon_product::product",
                generate_error_message(
                    "All tiles in the tile row/column do not have "
                    "equal height/width"));
        }
        std::vector<std::size_t> lhs_tile_row;
        std::vector<std::size_t> rhs_tile_col;
        std::size_t count = 0;
        // This could maybe be replaced with a std::copy_if
        if (lhs_localities.tiles_.size() != rhs_localities.tiles_.size() &&
            lhs_localities.tiles_.size() != 1 &&
            rhs_localities.tiles_.size() != 1)
        {
            HPX_THROW_EXCEPTION(hpx::bad_parameter,
                "dist_cannon_product::product",
                generate_error_message(
                    "number of tiles in lhs and rhs must be equal"));
        }
        for (int i = 0; i < lhs_localities.tiles_.size(); i++)
        {
            // This works because lhs_localities.tiles_ is
            // guaranteed to be sorted by locality index
            auto const& tmp_lhs_tile = lhs_localities.tiles_[i];
            auto const& tmp_rhs_tile = rhs_localities.tiles_[i];
            if (tmp_lhs_tile.spans_[1].start_ == lhs_row_span.start_ &&
                tmp_lhs_tile.spans_[1].size() == lhs_row_span.size())
            {
                lhs_tile_row.push_back(count);
            }
            if (tmp_rhs_tile.spans_[0].start_ == rhs_col_span.start_ &&
                tmp_rhs_tile.spans_[0].size() == rhs_col_span.size())
            {
                rhs_tile_col.push_back(count);
            }
            count++;
        }
        if (lhs_tile_row.size() < 2 || rhs_tile_col.size() < 2)
        {
            // This is debateable, maybe we should allow single tile
            // rows or columns
            HPX_THROW_EXCEPTION(hpx::bad_parameter,
                "dist_cannon_product::product",
                generate_error_message(
                    "cannon_product requires tile rows and columns of size at "
                    "least 2"));
        }
        std::int64_t max_start_col = 0;
        std::int64_t max_start_row = 0;
        for (int i = 0; i < lhs_tile_row.size(); i++)
        {
            std::size_t lhs_tile_idx = lhs_tile_row[i];
            std::size_t rhs_tile_idx = rhs_tile_col[i];
            std::size_t lhs_local_width =
                lhs_localities.tiles_[lhs_tile_idx].spans_[0].size();
            std::size_t rhs_local_height =
                rhs_localities.tiles_[rhs_tile_idx].spans_[1].size();
            if (lhs_localities.tiles_[lhs_tile_idx].spans_[0].start_ <
                    max_start_col ||
                lhs_local_width != lhs_col_span.size() ||
                rhs_localities.tiles_[rhs_tile_idx].spans_[1].start_ <
                    max_start_row ||
                rhs_local_height != rhs_row_span.size())
            {
                HPX_THROW_EXCEPTION(hpx::bad_parameter,
                    "dist_cannon_product::confirm_tile_validity",
                    generate_error_message(
                        "tiles not sorted in order of locality"));
            }
            max_start_col =
                lhs_localities.tiles_[lhs_tile_idx].spans_[0].start_;
            max_start_row =
                rhs_localities.tiles_[rhs_tile_idx].spans_[1].start_;
        }

        // construct a distributed matrix object for both tiles
        util::distributed_matrix<T> lhs_data(lhs_localities.annotation_.name_,
            lhs.matrix(), lhs_localities.locality_.num_localities_,
            lhs_localities.locality_.locality_id_);
        util::distributed_matrix<T> rhs_data(rhs_localities.annotation_.name_,
            rhs.matrix(), rhs_localities.locality_.num_localities_,
            rhs_localities.locality_.locality_id_);

        std::size_t lhs_local_tile_index = std::distance(lhs_tile_row.begin(),
            std::find(lhs_tile_row.begin(), lhs_tile_row.end(),
                lhs_localities.locality_.locality_id_));
        std::size_t rhs_local_tile_index = std::distance(rhs_tile_col.begin(),
            std::find(rhs_tile_col.begin(), rhs_tile_col.end(),
                rhs_localities.locality_.locality_id_));

        if (lhs_tile_row[lhs_local_tile_index] !=
                lhs_localities.locality_.locality_id_ ||
            rhs_tile_col[rhs_local_tile_index] !=
                rhs_localities.locality_.locality_id_)
        {
            HPX_THROW_EXCEPTION(hpx::bad_parameter,
                "dist_cannon_product::dot2d2d",
                generate_error_message(
                    "This locality not present in locality list"));
        }
        // This needs greater granularity to determine this
        blaze::DynamicMatrix<T> result_matrix(
            lhs.dimension(0), rhs.dimension(1), T{0});

        // 2d2d doesn't use every tile of the RHS, only those that contain
        // the rows with the same index as the columns the LHS has

        std::size_t lhs_iter_idx =
            (lhs_local_tile_index + 1) % lhs_tile_row.size();
        std::size_t rhs_iter_idx =
            (rhs_local_tile_index + 1) % rhs_tile_col.size();
        hpx::lcos::future<blaze::DynamicMatrix<T>> lhs_tmp1 =
            lhs_data.fetch(lhs_tile_row[lhs_iter_idx]);
        hpx::lcos::future<blaze::DynamicMatrix<T>> rhs_tmp1 =
            rhs_data.fetch(rhs_tile_col[rhs_iter_idx]);
        lhs_iter_idx = (lhs_iter_idx + 1) % lhs_tile_row.size();
        rhs_iter_idx = (rhs_iter_idx + 1) % rhs_tile_col.size();

        for (int i = 0; i < lhs_tile_row.size(); i++)
        {
            hpx::lcos::future<blaze::DynamicMatrix<T>> lhs_tmp2 =
                lhs_data.fetch(lhs_tile_row[lhs_iter_idx]);
            hpx::lcos::future<blaze::DynamicMatrix<T>> rhs_tmp2 =
                rhs_data.fetch(rhs_tile_col[rhs_iter_idx]);
            result_matrix += lhs_tmp1.get() * rhs_tmp1.get();
            lhs_iter_idx = (lhs_iter_idx + 1) % lhs_tile_row.size();
            rhs_iter_idx = (rhs_iter_idx + 1) % rhs_tile_col.size();
            lhs_tmp1 = std::move(lhs_tmp2);
            rhs_tmp1 = std::move(rhs_tmp2);
        }

        // collect overall result if left hand side vector is distributed
        // The overall result is a tiled matrix.
        execution_tree::primitive_argument_type result =
            execution_tree::primitive_argument_type{std::move(result_matrix)};

        execution_tree::annotation ann{ir::range("tile",
            ir::range("columns", rhs_localities.get_span(0).start_,
                rhs_localities.get_span(0).stop_),
            ir::range("rows", lhs_localities.get_span(1).start_,
                lhs_localities.get_span(1).stop_))};
        // Generate new tiling annotation for the result vector
        execution_tree::tiling_information_2d tile_info(ann, name_, codename_);

        ++lhs_localities.annotation_.generation_;

        auto locality_ann = lhs_localities.locality_.as_annotation();
        result.set_annotation(
            execution_tree::localities_annotation(locality_ann,
                tile_info.as_annotation(name_, codename_),
                lhs_localities.annotation_, name_, codename_),
            name_, codename_);

        return result;
    }

    // lhs_num_dims == 2
    // Multiply a matrix with a vector
    // Regular matrix multiplication
    template <typename T>
    execution_tree::primitive_argument_type dist_cannon_product::dot2d2d(
        ir::node_data<T>&& lhs, ir::node_data<T>&& rhs,
        execution_tree::localities_information&& lhs_localities,
        execution_tree::localities_information const& rhs_localities) const
    {
        if (lhs_localities.dimension() < 2 || rhs_localities.dimension() < 2)
        {
            HPX_THROW_EXCEPTION(hpx::bad_parameter,
                "dist_cannon_product::dot2d2d_par",
                generate_error_message(
                    "the operands have incompatible dimensionalities"));
        }

        if (lhs_localities.columns() != rhs_localities.rows())
        {
            HPX_THROW_EXCEPTION(hpx::bad_parameter,
                "dist_cannon_product::dot2d2d_par",
                generate_error_message(
                    "the operands have incompatible number of dimensions"));
        }

        return product(std::move(lhs), std::move(rhs),
            std::move(lhs_localities), rhs_localities);
    }
}}}    // namespace phylanx::dist_matrixops::primitives

#endif
