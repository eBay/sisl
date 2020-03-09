#pragma once

#include <utility/urcu_helper.hpp>
//#include <sds_logging/logging.hpp>
#include <boost/noncopyable.hpp>
#include <type_traits>

namespace sisl {

struct ImmutableSettings {
    std::string db_name = "monstor";
};

#define SETTINGS_SCHEMA_INIT(settings_type, schema_name)                                                               \
    sisl::SettingsFactory< settings_type >::instance().m_raw_schema =                                                  \
        std::string((const char*)&schema_name, (size_t)schema_name##_len);

template < typename SettingsT >
class SettingsFactory : boost::noncopyable {
    SettingsFactory() {}

public:
    static SettingsFactory& instance() {
        static SettingsFactory< SettingsT > s_instance;
        return s_instance;
    }

    // invoke a user callback and supply safely locked settings instance; unlock afteerwards
    template < typename CB >
    std::remove_reference_t< std::invoke_result_t< CB, const SettingsT& > > with_settings(CB cb) const {
        assert(m_rcu_data.get_node() != nullptr);
        auto settings = m_rcu_data.get(); // RAII
        const auto& s = const_cast< const SettingsT& >(*settings.get());
        using ret_t = std::invoke_result_t< CB, decltype(s) >;
        static_assert(/*std::is_fundamental_v<ret_t> || */ !std::is_pointer_v< ret_t > && !std::is_reference_v< ret_t >,
                      "Do you really want to return a referenence to an object? ");
        // if constexpr () {
        return cb(s);
        // } else {
        //     decltype(cb(s))::you_are_returning_object_of_type = 1;
        // }
    }

    void load(const std::string& config_file_path);

    // bool reload();

    /* this function is only used for unit testing */
    void save(const std::string& filepath);

    const std::string& get_current_settings() const { return m_current_settings; }

    const std::string& get_last_settings_error() const { return m_last_error; }

    const std::string get_json() const;

    const std::string get_local_config_file() const { return m_settings_file_path; }

    const std::string& get_version() const;

    SettingsT parse_config();

    const ImmutableSettings& immutable_settings() const { return m_i_settings; }

public:
    std::string m_raw_schema;

private:
    /* Unparsed settings string */
    std::string m_current_settings;

    /* Last settings parse error is stored in this string */
    std::string m_last_error;

    /* Settings file path */
    std::string m_settings_file_path;

    /* RCU protected settings data */
    urcu_data< SettingsT > m_rcu_data;

    ImmutableSettings m_i_settings;
};

#define WITH_SETTINGS(var, ...) with_settings([](const SettingsT& var) __VA_ARGS__)
#define WITH_SETTINGS_CAP1(var, cap1, ...) with_settings([cap1](const SettingsT& var) __VA_ARGS__)
#define WITH_SETTINGS_CAP2(var, cap1, cap2, ...) with_settings([ cap1, cap2 ](const SettingsT& var) __VA_ARGS__)
#define WITH_SETTINGS_PARAM(path_expr) with_settings([](const SettingsT& s_) { return s_.path_expr; })
#define WITH_SETTINGS_THIS(var, ...) with_settings([this](const SettingsT& var) __VA_ARGS__)
#define WITH_SETTINGS_THIS_CAP1(var, cap1, ...) with_settings([ this, cap1 ](const SettingsT& var) __VA_ARGS__)
#define WITH_SETTINGS_THIS_CAP2(var, cap1, cap2, ...)                                                                  \
    with_settings([ this, cap1, cap2 ](const SettingsT& var) __VA_ARGS__)

#define SETTINGS_FACTORY ::sisl::SettingsFactory::instance()

/*
 * SETTINGS(var) invokes user supplied lamdba passing it a safe pointer to an instance of settings object
 * naming it with var
 */
#define SETTINGS(var, ...) SETTINGS_FACTORY.WITH_SETTINGS(var, __VA_ARGS__)
#define SETTINGS_CAP1(var, cap1, ...) SETTINGS_FACTORY.WITH_SETTINGS_CAP1(var, cap1, __VA_ARGS__)
#define SETTINGS_CAP2(var, cap1, cap2, ...) SETTINGS_FACTORY.WITH_SETTINGS_CAP2(var, cap1, cap2, __VA_ARGS__)

/*
 * same as above but for lambdas that capture this
 */
#define SETTINGS_THIS(var, ...) SETTINGS_FACTORY.WITH_SETTINGS_THIS(var, __VA_ARGS__)
#define SETTINGS_THIS_CAP1(var, cap1, ...) SETTINGS_FACTORY.WITH_SETTINGS_THIS_CAP1(var, cap1, __VA_ARGS__)
#define SETTINGS_THIS_CAP2(var, cap1, cap2, ...) SETTINGS_FACTORY.WITH_SETTINGS_THIS_CAP2(var, cap1, cap2, __VA_ARGS__)

#define SETTINGS_PARAM(path_expr) SETTINGS_FACTORY.WITH_SETTINGS_PARAM(path_expr)
} // namespace sisl
