//
// Created by Kadayam, Hari on 28/03/18.
//
#ifndef FLIP_FLIP_HPP
#define FLIP_FLIP_HPP

#include "proto/flip_spec.pb.h"
#include <atomic>
#include <tuple>
#include <functional>
#include <boost/optional.hpp>
#include <glog/logging.h>

namespace flip {
template <
        size_t Index = 0, // start iteration at 0 index
        typename TTuple,  // the tuple type
        size_t Size =
        std::tuple_size_v<
                std::remove_reference_t<TTuple>>, // tuple size
        typename TCallable, // the callable to bo invoked for each tuple item
        typename... TArgs   // other arguments to be passed to the callable
>
void for_each(TTuple&& tuple, TCallable&& callable, TArgs&&... args)
{
    if constexpr (Index < Size)
    {
        std::invoke(callable, args..., std::get<Index>(tuple));

        if constexpr (Index + 1 < Size)
            for_each<Index + 1>(
                    std::forward<TTuple>(tuple),
                    std::forward<TCallable>(callable),
                    std::forward<TArgs>(args)...);
    }
}

struct flip_name_compare {
    bool operator()(const std::string &lhs, const std::string &rhs) const {
        return lhs < rhs;
    }
};

struct flip_instance {
    flip_instance(const FlipSpec &fspec) :
            m_fspec(fspec),
            m_hit_count(0),
            m_exec_count(0) {
    }

    flip_instance(const flip_instance &other) {
        m_fspec = other.m_fspec;
        m_hit_count.store(other.m_hit_count.load());
        m_exec_count.store(other.m_exec_count.load());
    }

    FlipSpec m_fspec;
    std::atomic< uint32_t > m_hit_count;
    std::atomic< uint32_t > m_exec_count;
};

template <typename T>
struct val_converter {
    T operator()(const ParamValue &val) {
        return 0;
    }
};

template <>
struct val_converter<int> {
    int operator()(const ParamValue &val) {
        return (val.kind_case() == ParamValue::kIntValue) ? val.int_value() : 0;
    }
};

#if 0
template <>
struct val_converter<const int> {
    const int operator()(const ParamValue &val) {
        return (val.kind_case() == ParamValue::kIntValue) ? val.int_value() : 0;
    }
};
#endif

template <>
struct val_converter<long> {
    long operator()(const ParamValue &val) {
        return (val.kind_case() == ParamValue::kLongValue) ? val.long_value() : 0;
    }
};

template <>
struct val_converter<double> {
    double operator()(const ParamValue &val) {
        return (val.kind_case() == ParamValue::kDoubleValue) ? val.double_value() : 0;
    }
};

template <>
struct val_converter<std::string> {
    std::string operator()(const ParamValue &val) {
        return (val.kind_case() == ParamValue::kStringValue) ? val.string_value() : "";
    }
};

template <>
struct val_converter<bool> {
    bool operator()(const ParamValue &val) {
        return (val.kind_case() == ParamValue::kBoolValue) ? val.bool_value() : 0;
    }
};

class Flip {
public:
    Flip() {
    }

    bool add(const FlipSpec &fspec) {
        auto inst = flip_instance(fspec);

        // TODO: Add verification to see if the flip is already scheduled, any errors etc..
        m_flip_specs.emplace(std::pair< std::string, flip_instance >(fspec.flip_name(), inst));

        LOG(INFO) << "Added new fault flip " << fspec.flip_name() << " to the list of flips";
        return true;
    }

    template< class... Args >
    bool test_flip(std::string flip_name, Args &&... args) {
        auto search = m_flip_specs.find(flip_name);
        if (search == m_flip_specs.end()) {
            //LOG(INFO) << "Flip " << flip_name << " is not triggered";
            return false;
        }

        auto &inst = search->second;
        auto fspec = inst.m_fspec;
        std::tuple<Args...> arglist(std::forward<Args>(args)...);

        auto i = 0U;
        bool matched = true;
        for_each(arglist, [this, fspec, &i, &matched](auto &v) {
            if (!condition_matches(fspec.conditions()[i++], v)) {
                matched = false;
            }
        });

        // One or more conditions does not match.
        if (!matched) {
            //LOG(INFO)  << "Flip " << flip_name << " does not match with one of the condition";
            return false;
        }

        // Have we already executed this enough times
        auto count = fspec.flip_frequency().count();
        if (count && (inst.m_exec_count.load(std::memory_order_acquire) >= count)) {
            LOG(INFO)  << "Flip " << flip_name << " matches, but it reached max count = " << count;
            return false;
        }

        if (!handle_hits(fspec.flip_frequency(), inst)) {
            LOG(INFO)  << "Flip " << flip_name << " matches, but it is rate limited";
            return false;
        }

        inst.m_exec_count.fetch_add(1, std::memory_order_acq_rel);
        LOG(INFO)  << "Flip " << flip_name << " matches and hits";
        return true;
    }

    template< typename T, class... Args >
    boost::optional< T > get_test_flip(std::string flip_name, Args &&... args) {
        auto search = m_flip_specs.find(flip_name);
        if (search == m_flip_specs.end()) {
            LOG(INFO)  << "Flip " << flip_name << " is not triggered";
            return boost::none;
        }

        auto &inst = search->second;
        auto fspec = inst.m_fspec;
        std::tuple<Args...> arglist(std::forward<Args>(args)...);

        auto i = 0U;
        bool matched = true;
        for_each(arglist, [this, fspec, &i, &matched](auto &v) {
            if (!condition_matches(fspec.conditions()[i++], v)) {
                matched = false;
            }
        });

        // One or more conditions does not match.
        if (!matched) {
            return boost::none;
        }

        // Have we already executed this enough times
        if (inst.m_exec_count.load(std::memory_order_acquire) >= fspec.flip_frequency().count()) {
            LOG(INFO)  << "Flip " << flip_name << " matches, but it reached max count = "
                      << fspec.flip_frequency().count();
            return boost::none;
        }

        if (!handle_hits(fspec.flip_frequency(), inst)) {
            LOG(INFO)  << "Flip " << flip_name << " matches, but it is rate limited";
            return boost::none;
        }

        inst.m_exec_count.fetch_add(1, std::memory_order_acq_rel);
        LOG(INFO)  << "Flip " << flip_name << " matches and hits";

        return val_converter< T >()(fspec.returns());
    }

private:
    template< typename T >
    bool condition_matches(const FlipCondition &cond, T &comp_val) {
        auto val1 = val_converter< T >()(cond.value());
        return compare_val< T >(val1, comp_val, cond.oper());
    }

    bool handle_hits(const FlipFrequency &freq, flip_instance &inst) {
        auto hit_count = inst.m_hit_count.fetch_add(1, std::memory_order_release);
        if (freq.every_nth() != 0) {
            return ((hit_count % freq.every_nth()) == 0);
        } else {
            return ((rand() % 100) < freq.percent());
        }
    }

    template< typename T >
    bool compare_val(T &val1, T &val2, Operator oper) {
        switch (oper) {
        case Operator::DONT_CARE:
            return true;

        case Operator::EQUAL:
            return (val1 == val2);

        case Operator::NOT_EQUAL:
            return (val1 != val2);

        case Operator::GREATER_THAN:
            return (val1 > val2);

        case Operator::LESS_THAN:
            return (val1 < val2);

        case Operator::GREATER_THAN_OR_EQUAL:
            return (val1 >= val2);

        case Operator::LESS_THAN_OR_EQUAL:
            return (val1 <= val2);

        default:
            return false;
        }
    }

private:
    std::map< std::string, flip_instance, flip_name_compare > m_flip_specs;
};

} // namespace flip
#endif //FLIP_FLIP_HPP
