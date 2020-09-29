//
// Created by Kadayam, Hari on 28/03/18.
//
#ifndef FLIP_FLIP_HPP
#define FLIP_FLIP_HPP

#include "flip_spec.pb.h"
#include "flip_rpc_server.hpp"
#include <atomic>
#include <tuple>
#include <functional>
#include <boost/optional.hpp>
#include <boost/variant.hpp>
#include <sds_logging/logging.h>
#include <shared_mutex>
#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <cstdlib>
#include <string>
#include <regex>

SDS_LOGGING_DECL(flip)

namespace flip {

template < size_t Index = 0,                                                     // start iteration at 0 index
           typename TTuple,                                                      // the tuple type
           size_t Size = std::tuple_size_v< std::remove_reference_t< TTuple > >, // tuple size
           typename TCallable, // the callable to bo invoked for each tuple item
           typename... TArgs   // other arguments to be passed to the callable
           >
void for_each(TTuple&& tuple, TCallable&& callable, TArgs&&... args) {
    if constexpr (Index < Size) {
        std::invoke(callable, args..., std::get< Index >(tuple));

        if constexpr (Index + 1 < Size) {
            for_each< Index + 1 >(std::forward< TTuple >(tuple), std::forward< TCallable >(callable),
                                  std::forward< TArgs >(args)...);
        }
    }
}

struct flip_name_compare {
    bool operator()(const std::string& lhs, const std::string& rhs) const { return lhs < rhs; }
};

struct flip_instance {
    flip_instance(const FlipSpec& fspec) :
            m_fspec(fspec),
            m_hit_count(0),
            m_remain_exec_count(fspec.flip_frequency().count()) {}

    flip_instance(const flip_instance& other) {
        m_fspec = other.m_fspec;
        m_hit_count.store(other.m_hit_count.load());
        m_remain_exec_count.store(other.m_remain_exec_count.load());
    }

    std::string to_string() const {
        std::stringstream ss;
        ss << "\n---------------------------" << m_fspec.flip_name() << "-----------------------\n";
        ss << "Hitcount: " << m_hit_count << "\n";
        ss << "Remaining count: " << m_remain_exec_count << "\n";
        ss << m_fspec.flip_frequency().DebugString();
        ss << m_fspec.flip_action().DebugString();
        ss << "Conditions: [\n";
        auto i = 1;
        for (const auto& cond : m_fspec.conditions()) {
            ss << std::to_string(i) << ") " << Operator_Name(cond.oper()) << " => " << cond.value().DebugString();
            ++i;
        }
        ss << "]";
        ss << "\n-------------------------------------------------------------------\n";
        return ss.str();
    }

    FlipSpec m_fspec;
    std::atomic< uint32_t > m_hit_count;
    std::atomic< int32_t > m_remain_exec_count;
};

/****************************** Proto Param to Value converter ******************************/
template < typename T >
struct val_converter {
    T operator()(const ParamValue& val) { return 0; }
};

template <>
struct val_converter< int > {
    int operator()(const ParamValue& val) { return (val.kind_case() == ParamValue::kIntValue) ? val.int_value() : 0; }
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
struct val_converter< long > {
    long operator()(const ParamValue& val) {
        return (val.kind_case() == ParamValue::kLongValue) ? val.long_value() : 0;
    }
};

template <>
struct val_converter< double > {
    double operator()(const ParamValue& val) {
        return (val.kind_case() == ParamValue::kDoubleValue) ? val.double_value() : 0;
    }
};

template <>
struct val_converter< std::string > {
    std::string operator()(const ParamValue& val) {
        return (val.kind_case() == ParamValue::kStringValue) ? val.string_value() : "";
    }
};

template <>
struct val_converter< const char* > {
    const char* operator()(const ParamValue& val) {
        return (val.kind_case() == ParamValue::kStringValue) ? val.string_value().c_str() : nullptr;
    }
};

template <>
struct val_converter< bool > {
    bool operator()(const ParamValue& val) {
        return (val.kind_case() == ParamValue::kBoolValue) ? val.bool_value() : 0;
    }
};

template < typename T >
struct delayed_return_param {
    uint64_t delay_usec;
    T val;
};

template < typename T >
struct val_converter< delayed_return_param< T > > {
    delayed_return_param< T > operator()(const ParamValue& val) {
        delayed_return_param< T > dummy;
        return dummy;
    }
};

/******************************************** Value to Proto converter ****************************************/
template < typename T >
struct to_proto_converter {
    void operator()(const T& val, ParamValue* out_pval) {}
};

template <>
struct to_proto_converter< int > {
    void operator()(const int& val, ParamValue* out_pval) { out_pval->set_int_value(val); }
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
struct to_proto_converter< long > {
    void operator()(const long& val, ParamValue* out_pval) { out_pval->set_long_value(val); }
};

template <>
struct to_proto_converter< double > {
    void operator()(const double& val, ParamValue* out_pval) { out_pval->set_double_value(val); }
};

template <>
struct to_proto_converter< std::string > {
    void operator()(const std::string& val, ParamValue* out_pval) { out_pval->set_string_value(val); }
};

template <>
struct to_proto_converter< const char* > {
    void operator()(const char*& val, ParamValue* out_pval) { out_pval->set_string_value(val); }
};

template <>
struct to_proto_converter< bool > {
    void operator()(const bool& val, ParamValue* out_pval) { out_pval->set_bool_value(val); }
};

/******************************************* Comparators *************************************/
template < typename T >
struct compare_val {
    bool operator()(const T& val1, const T& val2, Operator oper) {
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
};

template <>
struct compare_val< std::string > {
    bool operator()(const std::string& val1, const std::string& val2, Operator oper) {
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

        case Operator::REG_EX: {
            const std::regex re(val2);
            return (std::sregex_iterator(val1.begin(), val1.end(), re) != std::sregex_iterator());
        }

        default:
            return false;
        }
    }
};
template <>
struct compare_val< const char* > {
    bool operator()(const char*& val1, const char*& val2, Operator oper) {
        switch (oper) {
        case Operator::DONT_CARE:
            return true;

        case Operator::EQUAL:
            return (val1 && val2 && (strcmp(val1, val2) == 0)) || (!val1 && !val2);

        case Operator::NOT_EQUAL:
            return (val1 && val2 && (strcmp(val1, val2) != 0)) || (!val1 && val2) || (val1 && !val2);

        case Operator::GREATER_THAN:
            return (val1 && val2 && (strcmp(val1, val2) > 0)) || (val1 && !val2);

        case Operator::LESS_THAN:
            return (val1 && val2 && (strcmp(val1, val2) < 0)) || (!val1 && val2);

        case Operator::GREATER_THAN_OR_EQUAL:
            return (val1 && val2 && (strcmp(val1, val2) >= 0)) || (val1 && !val2) || (!val1 && !val2);

        case Operator::LESS_THAN_OR_EQUAL:
            return (val1 && val2 && (strcmp(val1, val2) <= 0)) || (!val1 && val2) || (!val1 && !val2);

        case Operator::REG_EX: {
            const std::regex re(val2);
            const std::string v(val1);
            return (std::sregex_iterator(v.begin(), v.end(), re) != std::sregex_iterator());
        }

        default:
            return false;
        }
    }
};

using io_service = boost::asio::io_service;
using deadline_timer = boost::asio::deadline_timer;
using io_work = boost::asio::io_service::work;

class FlipTimerBase {
public:
    virtual void schedule(boost::posix_time::time_duration delay_us, const std::function< void() >& closure) = 0;
};

class FlipTimerAsio : public FlipTimerBase {
public:
    FlipTimerAsio() : m_timer_count(0) {}
    ~FlipTimerAsio() {
        if (m_timer_thread != nullptr) {
            m_work.reset();
            m_timer_thread->join();
        }
    }

    void schedule(boost::posix_time::time_duration delay_us, const std::function< void() >& closure) override {
        std::unique_lock< std::mutex > lk(m_thr_mutex);
        ++m_timer_count;
        if (m_work == nullptr) {
            m_work = std::make_unique< io_work >(m_svc);
            m_timer_thread = std::make_unique< std::thread >(std::bind(&FlipTimerAsio::timer_thr, this));
        }

        auto t = std::make_shared< deadline_timer >(m_svc, delay_us);
        t->async_wait([this, closure, t](const boost::system::error_code& e) {
            if (e) {
                LOGERRORMOD(flip, "Error in timer routine, message {}", e.message());
            } else {
                closure();
            }
            std::unique_lock< std::mutex > lk(m_thr_mutex);
            --m_timer_count;
        });
    }

    void timer_thr() {
        size_t executed = 0;
        executed = m_svc.run();
        // To suppress compiler warning
        (void)executed;
    }

private:
    io_service m_svc;
    std::unique_ptr< io_work > m_work;
    std::mutex m_thr_mutex;
    int32_t m_timer_count;
    std::unique_ptr< std::thread > m_timer_thread;
};

#define TEST_ONLY 0
#define RETURN_VAL 1
#define SET_DELAY 2
#define DELAYED_RETURN 3

class Flip {
public:
    Flip() : m_flip_enabled(false) {}

    static Flip& instance() {
        static Flip s_instance;
        return s_instance;
    }

    void start_rpc_server() {
        m_flip_server_thread = std::unique_ptr< std::thread >(new std::thread(FlipRPCServer::rpc_thread));
        m_flip_server_thread->detach();
    }

    bool add(const FlipSpec& fspec) {
        m_flip_enabled = true;
        auto inst = flip_instance(fspec);

        // LOG(INFO) << "Fpsec: " << fspec.DebugString();

        // TODO: Add verification to see if the flip is already scheduled, any errors etc..
        std::unique_lock< std::shared_mutex > lock(m_mutex);

        // Create a timer instance only when we have delays/delayreturns flip added
        auto action_type = fspec.flip_action().action_case();
        if ((action_type == FlipAction::kDelays) || (action_type == FlipAction::kDelayReturns)) {
            if (m_timer == nullptr) { m_timer = std::make_unique< FlipTimerAsio >(); }
        }
        m_flip_specs.emplace(std::pair< std::string, flip_instance >(fspec.flip_name(), inst));
        LOGDEBUGMOD(flip, "Added new fault flip {} to the list of flips", fspec.flip_name());
        // LOG(INFO) << "Flip details:" << inst.to_string();
        return true;
    }

    std::vector< std::string > get(const std::string& flip_name) {
        std::shared_lock< std::shared_mutex > lock(m_mutex);
        std::vector< std::string > res;

        auto search = m_flip_specs.equal_range(flip_name);
        for (auto it = search.first; it != search.second; ++it) {
            const auto& inst = it->second;
            res.emplace_back(inst.to_string());
        }

        return res;
    }

    std::vector< std::string > get_all() {
        std::shared_lock< std::shared_mutex > lock(m_mutex);
        std::vector< std::string > res;

        for (const auto& it : m_flip_specs) {
            const auto& inst = it.second;
            res.emplace_back(inst.to_string());
#if 0
            for (auto it = inst_range.first; it != inst_range.second; ++it) {
                const auto& inst = inst_range->second;
                res.emplace_back(inst.to_string());
            }
#endif
        }

        return res;
    }
#if 0
    bool add_flip(std::string flip_name, std::vector<FlipCondition&> conditions, FlipAction& action,
            uint32_t count, uint8_t percent) {
        FlipSpec fspec;
        *(fspec.mutable_flip_name()) = "delay_ret_fspec";

        auto cond = fspec->mutable_conditions()->Add();
        *cond->mutable_name() = "cmd_type";
        cond->set_oper(flip::Operator::EQUAL);
        cond->mutable_value()->set_int_value(2);

        fspec->mutable_flip_action()->mutable_delay_returns()->set_delay_in_usec(100000);
        fspec->mutable_flip_action()->mutable_delay_returns()->mutable_return_()->set_string_value("Delayed error simulated value");

        auto freq = fspec->mutable_flip_frequency();
        freq->set_count(2);
        freq->set_percent(100);
    }
#endif

    template < class... Args >
    bool test_flip(std::string flip_name, Args&&... args) {
        if (!m_flip_enabled) return false;
        auto ret = __test_flip< bool, TEST_ONLY >(flip_name, std::forward< Args >(args)...);
        return (ret != boost::none);
    }

    template < typename T, class... Args >
    boost::optional< T > get_test_flip(std::string flip_name, Args&&... args) {
        if (!m_flip_enabled) return boost::none;

        auto ret = __test_flip< T, RETURN_VAL >(flip_name, std::forward< Args >(args)...);
        if (ret == boost::none) return boost::none;
        return boost::optional< T >(boost::get< T >(ret.get()));
    }

    template < class... Args >
    bool delay_flip(std::string flip_name, const std::function< void() >& closure, Args&&... args) {
        if (!m_flip_enabled) return false;

        auto ret = __test_flip< bool, SET_DELAY >(flip_name, std::forward< Args >(args)...);
        if (ret == boost::none) return false; // Not a hit

        uint64_t delay_usec = boost::get< uint64_t >(ret.get());
        get_timer().schedule(boost::posix_time::microseconds(delay_usec), closure);
        return true;
    }

    template < typename T, class... Args >
    bool get_delay_flip(std::string flip_name, const std::function< void(T) >& closure, Args&&... args) {
        if (!m_flip_enabled) return false;

        auto ret = __test_flip< T, DELAYED_RETURN >(flip_name, std::forward< Args >(args)...);
        if (ret == boost::none) return false; // Not a hit

        auto param = boost::get< delayed_return_param< T > >(ret.get());
        LOGDEBUGMOD(flip, "Returned param delay = {} val = {}", param.delay_usec, param.val);
        get_timer().schedule(boost::posix_time::microseconds(param.delay_usec),
                             [closure, param]() { closure(param.val); });
        return true;
    }

    void override_timer(std::unique_ptr< FlipTimerBase > t) {
        std::unique_lock< std::shared_mutex > lock(m_mutex);
        m_timer = std::move(t);
    }

private:
    template < typename T, int ActionType, class... Args >
    boost::optional< boost::variant< T, bool, uint64_t, delayed_return_param< T > > > __test_flip(std::string flip_name,
                                                                                                  Args&&... args) {
        bool exec_completed = false; // If all the exec for the flip is completed.
        flip_instance* inst = nullptr;

        {
            std::shared_lock< std::shared_mutex > lock(m_mutex);
            inst = match_flip(flip_name, std::forward< Args >(args)...);
            if (inst == nullptr) {
                // LOG(INFO) << "Flip " << flip_name << " either not exist or conditions not match";
                return boost::none;
            }
            auto& fspec = inst->m_fspec;

            // Check if we are subjected to rate limit
            if (!handle_hits(fspec.flip_frequency(), inst)) {
                LOGDEBUGMOD(flip, "Flip {} matches, but it is rate limited", flip_name);
                return boost::none;
            }

            // Have we already executed this enough times
            auto remain_count = inst->m_remain_exec_count.fetch_sub(1, std::memory_order_acq_rel) - 1;
            if (remain_count == 0) {
                exec_completed = true;
            } else if (remain_count < 0) {
                LOGDEBUGMOD(flip, "Flip {} matches, but reaches max count", flip_name);
                return boost::none;
            }
            LOGDEBUGMOD(flip, "Flip {} matches and hits", flip_name);
        }

        boost::variant< T, bool, uint64_t, delayed_return_param< T > > val_ret;
        switch (inst->m_fspec.flip_action().action_case()) {
        case FlipAction::kReturns:
            if (ActionType == RETURN_VAL) {
                val_ret = val_converter< T >()(inst->m_fspec.flip_action().returns().retval());
            } else {
                val_ret = true;
            }
            break;

        case FlipAction::kNoAction:
            // static_assert(!std::is_same<ValueNeeded, true>::value || std::is_same<T, bool>::value, "__test_flip
            // without value should be called with bool as type");
            val_ret = true;
            break;

        case FlipAction::kDelays:
            val_ret = inst->m_fspec.flip_action().delays().delay_in_usec();
            break;

        case FlipAction::kDelayReturns:
            if (ActionType == DELAYED_RETURN) {
                auto& flip_dr = inst->m_fspec.flip_action().delay_returns();
                delayed_return_param< T > dr;
                dr.delay_usec = flip_dr.delay_in_usec();
                dr.val = val_converter< T >()(flip_dr.retval());
                val_ret = dr;
            } else {
                val_ret = true;
            }
            break;

        default:
            val_ret = true;
        }

        if (exec_completed) {
            // If we completed the execution, need to remove them
            std::unique_lock< std::shared_mutex > lock(m_mutex);
            if (inst->m_remain_exec_count.load(std::memory_order_relaxed) == 0) { m_flip_specs.erase(flip_name); }
        }
        return val_ret;
    }

    template < class... Args >
    flip_instance* match_flip(std::string flip_name, Args&&... args) {
        flip_instance* match_inst = nullptr;

        auto search = m_flip_specs.equal_range(flip_name);
        for (auto it = search.first; it != search.second; ++it) {
            auto inst = &it->second;
            auto fspec = inst->m_fspec;

            // Check for all the condition match
            std::tuple< Args... > arglist(std::forward< Args >(args)...);
            auto i = 0U;
            bool matched = true;
            for_each(arglist, [this, fspec, &i, &matched](auto& v) {
                if (!condition_matches(v, fspec.conditions()[i++])) { matched = false; }
            });

            // One or more conditions does not match.
            if (matched) {
                match_inst = inst;
                break;
            }
        }
        return match_inst;
    }

    template < typename T >
    bool condition_matches(T& comp_val, const FlipCondition& cond) {
        auto val1 = val_converter< T >()(cond.value());
        return compare_val< T >()(comp_val, val1, cond.oper());
    }

    bool handle_hits(const FlipFrequency& freq, flip_instance* inst) {
        auto hit_count = inst->m_hit_count.fetch_add(1, std::memory_order_release);
        if (freq.every_nth() != 0) {
            return ((hit_count % freq.every_nth()) == 0);
        } else {
            return ((uint32_t)(rand() % 100) < freq.percent());
        }
    }

    FlipTimerBase& get_timer() { return *m_timer; }

#if 0
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

    template<>
    bool compare_val(const char *&val1, const char *&val2, Operator oper) {
        switch (oper) {
        case Operator::DONT_CARE:
            return true;

        case Operator::EQUAL:
            return (val1 && val2 && (strcmp(val1, val2) == 0)) || (!val1 && !val2);

        case Operator::NOT_EQUAL:
            return (val1 && val2 && (strcmp(val1, val2) != 0)) || (!val1 && val2) || (val1 && !val2);

        case Operator::GREATER_THAN:
            return (val1 && val2 && (strcmp(val1, val2) > 0)) || (val1 && !val2);

        case Operator::LESS_THAN:
            return (val1 && val2 && (strcmp(val1, val2) < 0)) || (!val1 && val2);

        case Operator::GREATER_THAN_OR_EQUAL:
            return (val1 && val2 && (strcmp(val1, val2) >= 0)) || (val1 && !val2) || (!val1 && !val2);

        case Operator::LESS_THAN_OR_EQUAL:
            return (val1 && val2 && (strcmp(val1, val2) <= 0)) || (!val1 && val2) || (!val1 && !val2);

        default:
            return false;
        }
    }
#endif
private:
    std::multimap< std::string, flip_instance, flip_name_compare > m_flip_specs;
    std::shared_mutex m_mutex;
    bool m_flip_enabled;
    std::unique_ptr< FlipTimerBase > m_timer;
    std::unique_ptr< std::thread > m_flip_server_thread;
};

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
#endif // FLIP_FLIP_HPP
