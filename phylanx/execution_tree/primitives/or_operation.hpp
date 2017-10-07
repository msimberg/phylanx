//  Copyright (c) 2017 Hartmut Kaiser
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#if !defined(PHYLANX_PRIMITIVES_OR_OPERATION_OCT_06_2017_0522PM)
#define PHYLANX_PRIMITIVES_OR_OPERATION_OCT_06_2017_0522PM

#include <phylanx/config.hpp>
#include <phylanx/ast/node.hpp>
#include <phylanx/execution_tree/primitives/base_primitive.hpp>
#include <phylanx/ir/node_data.hpp>
#include <phylanx/util/optional.hpp>
#include <phylanx/util/serialization/optional.hpp>

#include <hpx/include/components.hpp>

#include <utility>
#include <vector>

namespace phylanx { namespace execution_tree { namespace primitives
{
    class HPX_COMPONENT_EXPORT or_operation
        : public base_primitive
        , public hpx::components::component_base<or_operation>
    {
    private:
        using operand_type = util::optional<ir::node_data<double>>;
        using operands_type = std::vector<operand_type>;

    public:
        static match_pattern_type const match_data;

        or_operation() = default;

        or_operation(std::vector<primitive_argument_type>&& operands);

        hpx::future<operand_type> eval() const override;

    private:
        std::vector<primitive_argument_type> operands_;
    };
}}}

#endif


