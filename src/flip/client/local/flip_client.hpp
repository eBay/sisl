/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Author/Developer(s): Harihara Kadayam
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 *
 *********************************************************************************/
#pragma once
#include "lib/flip.hpp"

namespace flip {
class FlipClient {
public:
    explicit FlipClient(Flip* f) : m_flip(f) {}

    template < typename T >
    void create_condition(const std::string& param_name, flip::Operator oper, const T& value,
                          FlipCondition* out_condition) {
        *(out_condition->mutable_name()) = param_name;
        out_condition->set_oper(oper);
        to_proto_converter< T >()(value, out_condition->mutable_value());
    }

    bool inject_noreturn_flip(std::string flip_name, const std::vector< FlipCondition >& conditions,
                              const FlipFrequency& freq) {
        FlipSpec fspec;

        _create_flip_spec(flip_name, conditions, freq, fspec);
        fspec.mutable_flip_action()->set_no_action(true);

        m_flip->add(fspec);
        return true;
    }

    template < typename T >
    bool inject_retval_flip(std::string flip_name, const std::vector< FlipCondition >& conditions,
                            const FlipFrequency& freq, const T& retval) {
        FlipSpec fspec;

        _create_flip_spec(flip_name, conditions, freq, fspec);
        to_proto_converter< T >()(retval, fspec.mutable_flip_action()->mutable_returns()->mutable_retval());

        m_flip->add(fspec);
        return true;
    }

    bool inject_delay_flip(std::string flip_name, const std::vector< FlipCondition >& conditions,
                           const FlipFrequency& freq, uint64_t delay_usec) {
        FlipSpec fspec;

        _create_flip_spec(flip_name, conditions, freq, fspec);
        fspec.mutable_flip_action()->mutable_delays()->set_delay_in_usec(delay_usec);

        m_flip->add(fspec);
        return true;
    }

    template < typename T >
    bool inject_delay_and_retval_flip(std::string flip_name, const std::vector< FlipCondition >& conditions,
                                      const FlipFrequency& freq, uint64_t delay_usec, const T& retval) {
        FlipSpec fspec;

        _create_flip_spec(flip_name, conditions, freq, fspec);
        fspec.mutable_flip_action()->mutable_delays()->set_delay_in_usec(delay_usec);
        to_proto_converter< T >()(retval, fspec.mutable_flip_action()->mutable_delay_returns()->mutable_retval());

        m_flip->add(fspec);
        return true;
    }

private:
    void _create_flip_spec(std::string flip_name, const std::vector< FlipCondition >& conditions,
                           const FlipFrequency& freq, FlipSpec& out_fspec) {
        *(out_fspec.mutable_flip_name()) = flip_name;
        for (auto& c : conditions) {
            *(out_fspec.mutable_conditions()->Add()) = c;
        }
        *(out_fspec.mutable_flip_frequency()) = freq;
    }

private:
    Flip* m_flip;
};
} // namespace flip