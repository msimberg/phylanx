//  Copyright (c) 2017-2018 Hartmut Kaiser
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <phylanx/config.hpp>
#include <phylanx/execution_tree/primitives/while_operation.hpp>
#include <phylanx/ir/node_data.hpp>

#include <hpx/include/lcos.hpp>
#include <hpx/include/naming.hpp>
#include <hpx/include/util.hpp>
#include <hpx/throw_exception.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

///////////////////////////////////////////////////////////////////////////////
namespace phylanx { namespace execution_tree { namespace primitives
{
    ///////////////////////////////////////////////////////////////////////////
    primitive create_while_operation(hpx::id_type const& locality,
        std::vector<primitive_argument_type>&& operands, std::string const& name)
    {
        static std::string type("while");
        return create_primitive_component(
            locality, type, std::move(operands), name);
    }

    match_pattern_type const while_operation::match_data =
    {
        hpx::util::make_tuple("while",
            std::vector<std::string>{"while(_1, _2)"},
            &create_while_operation, &create_primitive<while_operation>)
    };

    ///////////////////////////////////////////////////////////////////////////
    while_operation::while_operation(
            std::vector<primitive_argument_type>&& operands)
      : primitive_component_base(std::move(operands))
    {}

    namespace detail
    {
        struct iteration : std::enable_shared_from_this<iteration>
        {
            iteration(std::vector<primitive_argument_type> const& operands,
                    std::vector<primitive_argument_type> const& args)
              : operands_(operands)
              , args_(args)
            {
                if (operands_.size() != 2)
                {
                    HPX_THROW_EXCEPTION(hpx::bad_parameter,
                        "phylanx::execution_tree::primitives::while_operation::"
                            "while_operation",
                        "the while_operation primitive requires exactly two "
                            "arguments");
                }

                if (!valid(operands_[0]) || !valid(operands_[1]))
                {
                    HPX_THROW_EXCEPTION(hpx::bad_parameter,
                        "phylanx::execution_tree::primitives::while_operation::"
                            "while_operation",
                        "the while_operation primitive requires that the "
                            "arguments  by the operands array are valid");
                }
            }

            hpx::future<primitive_argument_type> body(
                hpx::future<primitive_argument_type>&& cond)
            {
                if (extract_boolean_value(cond.get()))
                {
                    // evaluate body of while statement
                    auto this_ = this->shared_from_this();
                    return literal_operand(operands_[1], args_).then(
                        [this_](
                            hpx::future<primitive_argument_type> && result
                        ) mutable
                        {
                            this_->result_ = result.get();
                            return this_->loop();
                        });
                }

                hpx::future<primitive_argument_type> f = p_.get_future();
                p_.set_value(std::move(result_));
                return f;
            }

            hpx::future<primitive_argument_type> loop()
            {
                // evaluate condition of while statement
                auto this_ = this->shared_from_this();
                return literal_operand(operands_[0], args_).then(
                    [this_](hpx::future<primitive_argument_type> && cond)
                    {
                        return this_->body(std::move(cond));
                    });
            }

        private:
            std::vector<primitive_argument_type> operands_;
            std::vector<primitive_argument_type> args_;
            hpx::promise<primitive_argument_type> p_;
            primitive_argument_type result_;
        };
    }

    // start iteration over given while statement
    hpx::future<primitive_argument_type> while_operation::eval(
        std::vector<primitive_argument_type> const& args) const
    {
        if (operands_.empty())
        {
            return std::make_shared<detail::iteration>(args, noargs)->loop();
        }

        return std::make_shared<detail::iteration>(operands_, args)->loop();
    }
}}}
