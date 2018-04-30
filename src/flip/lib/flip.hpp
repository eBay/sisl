//
// Created by Kadayam, Hari on 28/03/18.
//
#ifndef FLIP_FLIP_HPP
#define FLIP_FLIP_HPP

#include "flip_spec.pb.h"
#include <atomic>
#include <tuple>
#include <functional>
#include <boost/optional.hpp>
#include <boost/variant.hpp>
#include <glog/logging.h>
#include <shared_mutex>
#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <cstdlib>

namespace flip {
static thread_local boost::asio::io_service g_io;

template <
        size_t Index = 0, // start iteration at 0 index
        typename TTuple,  // the tuple type
        size_t Size =
        std::tuple_size_v<
                std::remove_reference_t<TTuple>>, // tuple size
        typename TCallable, // the callable to bo invoked for each tuple item
        typename... TArgs   // other arguments to be passed to the callable
>
void for_each(TTuple&& tuple, TCallable&& callable, TArgs&&... args) {
    if constexpr (Index < Size) {
        std::invoke(callable, args..., std::get<Index>(tuple));

        if constexpr (Index + 1 < Size) {
            for_each< Index + 1 >(
                    std::forward< TTuple >(tuple),
                    std::forward< TCallable >(callable),
                    std::forward< TArgs >(args)...);
        }
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
            m_remain_exec_count(fspec.flip_frequency().count()) {
    }

    flip_instance(const flip_instance &other) {
        m_fspec = other.m_fspec;
        m_hit_count.store(other.m_hit_count.load());
        m_remain_exec_count.store(other.m_remain_exec_count.load());
    }

    FlipSpec m_fspec;
    std::atomic< uint32_t > m_hit_count;
    std::atomic< int32_t > m_remain_exec_count;
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
    Flip() : m_flip_enabled(false) {
    }

    bool add(const FlipSpec &fspec) {
        m_flip_enabled = true;
        auto inst = flip_instance(fspec);

        // TODO: Add verification to see if the flip is already scheduled, any errors etc..
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_flip_specs.emplace(std::pair< std::string, flip_instance >(fspec.flip_name(), inst));
        LOG(INFO) << "Added new fault flip " << fspec.flip_name() << " to the list of flips";
        return true;
    }

    template< class... Args >
    bool test_flip(std::string flip_name, Args &&... args) {
#if 0
        std::shared_lock<std::shared_mutex> lock(m_mutex);

        auto inst = match_flip(flip_name, std::forward< Args >(args)...);
        if (inst == nullptr) {
            //LOG(INFO) << "Flip " << flip_name << " either not exist or conditions not match";
            return false;
        }
        auto &fspec = inst->m_fspec;

        // Have we already executed this enough times
        auto count = fspec.flip_frequency().count();
        if (count && (inst->m_remain_exec_count.load(std::memory_order_acquire) >= count)) {
            LOG(INFO)  << "Flip " << flip_name << " matches, but it reached max count = " << count;
            return false;
        }

        if (!handle_hits(fspec.flip_frequency(), inst)) {
            LOG(INFO)  << "Flip " << flip_name << " matches, but it is rate limited";
            return false;
        }

        inst->m_remain_exec_count.fetch_add(1, std::memory_order_acq_rel);
        LOG(INFO)  << "Flip " << flip_name << " matches and hits";
        return true;
#endif
        if (!m_flip_enabled) return false;
        auto ret = __test_flip<bool, false>(flip_name, std::forward< Args >(args)...);
        return (ret != boost::none);
    }

    template< typename T, class... Args >
    boost::optional< T > get_test_flip(std::string flip_name, Args &&... args) {
#if 0
        std::shared_lock<std::shared_mutex> lock(m_mutex);

        auto inst = match_flip(flip_name, std::forward< Args >(args)...);
        if (inst == nullptr) {
            //LOG(INFO) << "Flip " << flip_name << " either not exist or conditions not match";
            return boost::none;
        }
        auto &fspec = inst->m_fspec;

        // Have we already executed this enough times
        if (inst->m_remain_exec_count.load(std::memory_order_acquire) >= fspec.flip_frequency().count()) {
            LOG(INFO)  << "Flip " << flip_name << " matches, but it reached max count = "
                      << fspec.flip_frequency().count();
            return boost::none;
        }

        if (!handle_hits(fspec.flip_frequency(), inst)) {
            LOG(INFO)  << "Flip " << flip_name << " matches, but it is rate limited";
            return boost::none;
        }

        inst->m_remain_exec_count.fetch_add(1, std::memory_order_acq_rel);
        LOG(INFO)  << "Flip " << flip_name << " matches and hits";

        return val_converter< T >()(fspec.returns());
#endif
        if (!m_flip_enabled) return boost::none;

        auto ret = __test_flip<T, true>(flip_name, std::forward< Args >(args)...);
        if (ret == boost::none) return boost::none;
        return boost::optional<T>(boost::get<T>(ret.get()));
    }

    template< class... Args >
    bool delay_flip(std::string flip_name, const std::function<void()> &closure, Args &&... args) {
        if (!m_flip_enabled) return false;

        auto ret = __test_flip<bool, false>(flip_name, std::forward< Args >(args)...);
        if (ret != boost::none) {
            uint64_t delay_usec = boost::get<uint64_t>(ret.get());
            auto io = std::make_shared<boost::asio::io_service>();
            boost::asio::deadline_timer t(*io, boost::posix_time::milliseconds(delay_usec/1000));
            t.async_wait([closure, io](const boost::system::error_code& e) {
                closure();
            });
            auto ret = io->run();
            return true;
        } else {
            return false;
        }
    }

private:
    template< typename T, bool ValueNeeded, class... Args >
    boost::optional< boost::variant<T, bool, uint64_t> > __test_flip(std::string flip_name, Args &&... args) {
        bool exec_completed = false; // If all the exec for the flip is completed.
        flip_instance *inst = nullptr;

        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            inst = match_flip(flip_name, std::forward< Args >(args)...);
            if (inst == nullptr) {
                //LOG(INFO) << "Flip " << flip_name << " either not exist or conditions not match";
                return boost::none;
            }
            auto &fspec = inst->m_fspec;

            // Check if we are subjected to rate limit
            if (!handle_hits(fspec.flip_frequency(), inst)) {
                LOG(INFO) << "Flip " << flip_name << " matches, but it is rate limited";
                return boost::none;
            }

            // Have we already executed this enough times
            if (inst->m_remain_exec_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                exec_completed = true;
            }
            LOG(INFO) << "Flip " << flip_name << " matches and hits";
        }

        boost::variant<T, bool, uint64_t> val_ret ;
        switch (inst->m_fspec.flip_action().action_case()) {
        case FlipAction::kReturns:
            if (ValueNeeded) {
                val_ret = val_converter< T >()(inst->m_fspec.flip_action().returns().return_());
            } else {
                val_ret = true;
            }
            break;

        case FlipAction::kNoAction:
            //static_assert(!std::is_same<ValueNeeded, true>::value || std::is_same<T, bool>::value, "__test_flip without value should be called with bool as type");
            val_ret = true;
            break;

        case FlipAction::kDelays:
            val_ret = inst->m_fspec.flip_action().delays().delay_in_usec();
            break;

        default:
            val_ret = true;
        }

        if (exec_completed) {
            // If we completed the execution, need to remove them
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            if (inst->m_remain_exec_count.load(std::memory_order_relaxed) == 0) {
                m_flip_specs.erase(flip_name);
            }
        }
        return val_ret;
    }

    template< class... Args >
    flip_instance * match_flip(std::string flip_name, Args &&... args) {
        flip_instance *match_inst = nullptr;

        auto search = m_flip_specs.equal_range(flip_name);
        for (auto it = search.first; it != search.second; ++it) {
            auto inst = &it->second;
            auto fspec = inst->m_fspec;

            // Check for all the condition match
            std::tuple< Args... > arglist(std::forward< Args >(args)...);
            auto i = 0U;
            bool matched = true;
            for_each(arglist, [this, fspec, &i, &matched](auto &v) {
                if (!condition_matches(fspec.conditions()[i++], v)) {
                    matched = false;
                }
            });

            // One or more conditions does not match.
            if (matched) {
                match_inst = inst;
                break;
            }
        }
        return match_inst;
    }

    template< typename T >
    bool condition_matches(const FlipCondition &cond, T &comp_val) {
        auto val1 = val_converter< T >()(cond.value());
        return compare_val< T >(val1, comp_val, cond.oper());
    }

    bool handle_hits(const FlipFrequency &freq, flip_instance *inst) {
        auto hit_count = inst->m_hit_count.fetch_add(1, std::memory_order_release);
        if (freq.every_nth() != 0) {
            return ((hit_count % freq.every_nth()) == 0);
        } else {
            return ((uint32_t)(rand() % 100) < freq.percent());
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
    std::multimap< std::string, flip_instance, flip_name_compare > m_flip_specs;
    std::shared_mutex m_mutex;
    bool m_flip_enabled;
};

} // namespace flip
#endif //FLIP_FLIP_HPP
