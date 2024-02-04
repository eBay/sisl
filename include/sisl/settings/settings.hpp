/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Author/Developer(s): Harihara Kadayam, Aditya Marella
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

#include <cassert>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>

#include <boost/algorithm/string/replace.hpp>
#include <boost/noncopyable.hpp>
#include <flatbuffers/idl.h>

#include <nlohmann/json.hpp>

#include <sisl/logging/logging.h>
#include <sisl/options/options.h>
#include <sisl/utility/urcu_helper.hpp>

#define SETTINGS_INIT(schema_type, schema_name)                                                                        \
    extern unsigned char schema_name##_fbs[];                                                                          \
    extern unsigned int schema_name##_fbs_len;                                                                         \
    class schema_name##_factory : public ::sisl::SettingsFactory< schema_type##T > {                                   \
    public:                                                                                                            \
        static schema_name##_factory& instance() {                                                                     \
            static schema_name##_factory s_instance;                                                                   \
            return s_instance;                                                                                         \
        }                                                                                                              \
                                                                                                                       \
        schema_name##_factory() :                                                                                      \
                ::sisl::SettingsFactory< schema_type##T >{BOOST_PP_STRINGIZE(schema_name), schema_name##_fbs,          \
                                                                             schema_name##_fbs_len} {}                 \
    };

#define SETTINGS_FACTORY(schema_name) schema_name##_factory::instance()

namespace sisl {
static bool diff_vector(const reflection::Schema* schema, const reflection::Field* field, flatbuffers::VectorOfAny* v1,
                        flatbuffers::VectorOfAny* v2);
static bool diff(const reflection::Schema* schema, const reflection::Object* schema_object,
                 const flatbuffers::Table* root, const flatbuffers::Table* old_root) {
    if (root == nullptr || old_root == nullptr) { return root == nullptr && old_root == nullptr; }

    for (auto field : *schema_object->fields()) {
        if (field->attributes() != nullptr && field->attributes()->LookupByKey("hotswap") != nullptr) { continue; }
        switch (field->type()->base_type()) {
        case reflection::BaseType::Int:
        case reflection::BaseType::UInt:
        case reflection::BaseType::None:
        case reflection::BaseType::UType:
        case reflection::BaseType::Bool:
        case reflection::BaseType::Byte:
        case reflection::BaseType::UByte:
        case reflection::BaseType::Short:
        case reflection::BaseType::UShort:
        case reflection::BaseType::Long:
        case reflection::BaseType::ULong: {
            auto a1 = flatbuffers::GetAnyFieldI(*old_root, *field);
            auto a2 = flatbuffers::GetAnyFieldI(*root, *field);
            if (a1 != a2) {
                // CVLOG(VMODULE_SETTINGS, 1) << field->name()->str() << " changed, old: " << a1 << ", new: " << a2;
                return true;
            }
            break;
        }

        case reflection::BaseType::Float:
        case reflection::BaseType::Double: {
            auto a1 = flatbuffers::GetAnyFieldF(*old_root, *field);
            auto a2 = flatbuffers::GetAnyFieldF(*root, *field);
            if (a1 != a2) {
                // CVLOG(VMODULE_SETTINGS, 1) << field->name()->str() << " changed, old: " << a1 << ", new: " << a2;
                return true;
            }
            break;
        }

        case reflection::BaseType::String: {
            auto s1 = flatbuffers::GetFieldS(*old_root, *field);
            auto s2 = flatbuffers::GetFieldS(*root, *field);
            if (s1 != nullptr && s2 != nullptr && s1->str() != s2->str()) {
                // CVLOG(VMODULE_SETTINGS, 1) << field->name()->str() << " changed, old: " << s1 << ", new: " << s2;
                return true;
            }
            break;
        }
        case reflection::BaseType::Vector: {
            auto v1 = flatbuffers::GetFieldAnyV(*old_root, *field);
            auto v2 = flatbuffers::GetFieldAnyV(*root, *field);
            auto restart = diff_vector(schema, field, v1, v2);
            if (restart) {
                // CVLOG(VMODULE_SETTINGS, 1)
                //    << "Field: " << field->name()->str() << ", vector has different old and new values";
                return true;
            }
            break;
        }

        case reflection::BaseType::Obj: {
            if (field->name()->str() != "processed") {
                auto object = (*schema->objects())[field->type()->index()];
                auto restart = diff(schema, object, flatbuffers::GetFieldT(*root, *field),
                                    flatbuffers::GetFieldT(*old_root, *field));
                if (restart) return true;
            }
            break;
        }

        default: {
            // Please do not use unions or arrays in settings. It's crazy!
            // LOG_ASSERT(false) << "reflection::BaseType::Union type in settings is not supported";
            break;
        }
        }
    }
    return false;
}

static bool diff_vector(const reflection::Schema* schema, const reflection::Field* field, flatbuffers::VectorOfAny* v1,
                        flatbuffers::VectorOfAny* v2) {
    /* both pointers are null, so return false */
    if (v1 == nullptr && v2 == nullptr) return false;

    /* null check and size check */
    if (v1 == nullptr || v2 == nullptr || v1->size() != v2->size()) return true;

    auto type = field->type()->element();
    switch (type) {
    case reflection::BaseType::Int:
    case reflection::BaseType::UInt:
    case reflection::BaseType::None:
    case reflection::BaseType::UType:
    case reflection::BaseType::Bool:
    case reflection::BaseType::Byte:
    case reflection::BaseType::UByte:
    case reflection::BaseType::Short:
    case reflection::BaseType::UShort:
    case reflection::BaseType::Long:
    case reflection::BaseType::ULong: {
        for (size_t idx = 0; idx < v1->size(); idx++) {
            auto oldval = flatbuffers::GetAnyVectorElemI(v1, type, idx);
            auto newval = flatbuffers::GetAnyVectorElemI(v2, type, idx);
            if (oldval != newval) {
                /*CVLOG(VMODULE_SETTINGS, 1)
                    << field->name()->str() << ", idx: " << idx << " changed, old: " << oldval << ", new: " << newval;
                 */
                return true;
            }
        }
        break;
    }
    case reflection::BaseType::Float:
    case reflection::BaseType::Double: {
        for (size_t idx = 0; idx < v1->size(); idx++) {
            auto oldval = flatbuffers::GetAnyVectorElemF(v1, type, idx);
            auto newval = flatbuffers::GetAnyVectorElemF(v2, type, idx);
            if (oldval != newval) {
                /*CVLOG(VMODULE_SETTINGS, 1)
                    << field->name()->str() << ", idx: " << idx << " changed, old: " << oldval << ", new: " << newval;*/
                return true;
            }
        }
        break;
    }
    case reflection::BaseType::String: {
        for (size_t idx = 0; idx < v1->size(); idx++) {
            auto oldval = flatbuffers::GetAnyVectorElemS(v1, type, idx);
            auto newval = flatbuffers::GetAnyVectorElemS(v2, type, idx);
            if (oldval != newval) {
                /* CVLOG(VMODULE_SETTINGS, 1)
                    << field->name()->str() << ", idx: " << idx << " changed, old: " << oldval << ", new: " << newval;
                 */
                return true;
            }
        }
        break;
    }

    case reflection::BaseType::Vector: {
        // LOG_ASSERT(false) << "Nested vector not supported as of flatbuffer 1.9.0";
        break;
    }

    case reflection::BaseType::Obj: {
        auto object = (*schema->objects())[field->type()->index()];
        for (size_t idx = 0; idx < v1->size(); idx++) {
            auto restart =
                diff(schema, object, flatbuffers::GetAnyVectorElemPointer< const flatbuffers::Table >(v1, idx),
                     flatbuffers::GetAnyVectorElemPointer< const flatbuffers::Table >(v2, idx));
            if (restart) return true;
        }
        break;
    }

    default: {
        // Please do not use unions or arrays in settings. It's crazy!
        // LOG_ASSERT(false) << "reflection::BaseType::Union type in settings is not supported";
        break;
    }
    }
    return false;
}

class SettingsFactoryBase : public boost::noncopyable {
public:
    virtual void load() = 0;
    virtual bool reload() = 0;
    virtual void save() = 0;
    virtual const std::string get_json() const = 0;

    void set_config_file(const std::string& file) { m_base_file = file; }

protected:
    std::string m_base_file;
};

class SettingsFactoryRegistry {
public:
    static SettingsFactoryRegistry& instance(const std::string& path = "",
                                             const std::vector< std::string >& override_cfgs = {}) {
        static SettingsFactoryRegistry _inst{path, override_cfgs};
        return _inst;
    }

    SettingsFactoryRegistry(const std::string& path = "", const std::vector< std::string >& override_cfgs = {});
    void register_factory(const std::string& s, SettingsFactoryBase* f);
    void unregister_factory(const std::string& s);

    bool reload_all();
    void save_all();
    nlohmann::json get_json() const;

private:
    mutable std::shared_mutex m_mtx;
    std::string m_config_path;
    std::unordered_map< std::string, SettingsFactoryBase* > m_factories;
    std::unordered_map< std::string, nlohmann::json > m_override_cfgs;
};

template < typename SettingsT >
class SettingsFactory : public sisl::SettingsFactoryBase {
protected:
    SettingsFactory(const std::string& schema_name, unsigned char* raw_fbs, const unsigned int raw_fbs_len) :
            m_schema_name{schema_name}, m_raw_schema{std::string((const char*)raw_fbs, (size_t)raw_fbs_len)} {
        SettingsFactoryRegistry::instance().register_factory(schema_name, (sisl::SettingsFactoryBase*)this);
    }

public:
    // invoke a user callback and supply safely locked settings instance; unlock afteerwards
    template < typename CB >
    std::remove_reference_t< std::invoke_result_t< CB, const SettingsT& > > with_settings(CB cb) const {
        assert(m_rcu_data.get_node() != nullptr);
        auto settings = m_rcu_data.get(); // RAII
        const auto& s = const_cast< const SettingsT& >(*settings.get());
        using ret_t = std::invoke_result_t< CB, decltype(s) >;
        static_assert(/*std::is_fundamental_v<ret_t> || */ !std::is_pointer_v< ret_t > && !std::is_reference_v< ret_t >,
                      "Do you really want to return a reference to an object? ");
        // if constexpr () {
        return cb(s);
        // } else {
        //     decltype(cb(s))::you_are_returning_object_of_type = 1;
        // }
    }

    void modifiable_settings(const auto& cb) {
        assert(m_rcu_data.get_node() != nullptr);
        auto settings = m_rcu_data.get(); // RAII
        return cb(*settings.get());
    }

    void load() override { load_file(m_base_file); }
    bool reload() override { return reload_file(m_base_file); }
    void save() override {
        if (m_base_file.length() != 0) { save(m_base_file); }
    }

    void load_file(const std::string& config_file) { load(config_file, true /* is_config_file */); }
    void load_json(const std::string& json_string) { load(json_string, false /* is_config_file */); }
    bool reload_file(const std::string& config_file) { return reload(config_file, true /* is_config_file */); }
    bool reload_json(const std::string& json_string) { return reload(json_string, false /* is_config_file */); }

    void save(const std::string& filepath) {
        std::string json;
        flatbuffers::Parser parser;
        parser.opts.strict_json = true;
        parser.opts.output_default_scalars_in_json = true;

        if (!parser.Parse(m_raw_schema.c_str())) {
            LOGERROR("Error in parsing schema file to save");
            return;
        }

        parser.builder_.Finish(
            SettingsT::TableType::Pack(parser.builder_, m_rcu_data.get_node()->get().get(), nullptr));

        std::string fname = filepath;
        boost::replace_all(fname, ".json", "");
        if (GenTextFile(parser, "", fname) == nullptr) { LOGERROR("Error in Saving json to file"); }
    }

    const std::string& get_current_settings() const { return m_current_settings; }

    const std::string& get_last_settings_error() const { return m_last_error; }

    const std::string get_json() const override {
        std::string json;
        flatbuffers::Parser parser;
        parser.opts.strict_json = true;
        parser.opts.output_default_scalars_in_json = true;

        if (!parser.Parse(m_raw_schema.c_str())) {
            LOGERROR("Error parsing flatbuffer settings schema");
            return json;
        }

        parser.builder_.Finish(
            SettingsT::TableType::Pack(parser.builder_, m_rcu_data.get_node()->get().get(), nullptr));
        if (GenText(parser, parser.builder_.GetBufferPointer(), &json) == nullptr) {
            LOGERROR("Error generating json from flatbuffer");
        }
        return json;
    }

private:
    void load(const std::string& config, bool is_config_file) {
        try {
            SettingsT new_settings;
            parse_config(config, is_config_file, new_settings);
            // post_process(true, &new_settings);
            m_rcu_data.make_and_exchange(std::move(new_settings));
        } catch (std::exception& e) {
            throw std::runtime_error(fmt::format("Exception reading config {} (errmsg = {})",
                                                 (is_config_file ? config : " in json"), e.what()));
        }
    }

    bool reload(const std::string& config, bool is_config_file) {
        try {
            SettingsT new_settings;
            parse_config(config, is_config_file /* is_config_file */, new_settings);
            /* post_process may reconfigure some settings, therefore this has to be called before taking diff */
            // post_process(false, &new_settings);

            if (check_restart_needed(&new_settings, m_rcu_data.get_node()->get())) {
                m_current_settings = ""; /* getSettings will return empty briefly before exiting */
                return true;
            } else {
                m_rcu_data.make_and_exchange(std::move(new_settings));
            }
        } catch (std::exception& e) {
            LOGERROR("Exception reading config {} (errmsg = {})", (is_config_file ? config : " in json"), e.what());
        }
        return false;
    }

    void parse_config(const std::string& config, bool is_file, SettingsT& out_settings) {
        std::string json_config_str;
        if (is_file) {
            if (!flatbuffers::LoadFile(config.c_str(), false, &json_config_str)) {
                m_last_error = "flatbuffer::LoadFile() returned false";
                throw std::invalid_argument(m_last_error);
            }
        } else {
            json_config_str = config;
        }

        flatbuffers::Parser parser;
        parser.opts.skip_unexpected_fields_in_json = true;

        if (!parser.Parse(m_raw_schema.c_str())) {
            // LOG(ERROR) << "Error parsing flatbuffer settings schema: " << parser.error_;
            m_last_error = parser.error_;
            throw std::invalid_argument(parser.error_);
        }
        if (!parser.Parse(json_config_str.c_str(), nullptr)) {
            // LOG(ERROR) << "Error parsing json config file: " << parser.error_;
            m_last_error = parser.error_;
            throw std::invalid_argument(parser.error_);
        }

        // LOG(INFO) << "Previous settings: '" << factory->m_current_settings << "'";

        /* parsing succeeded, update current settings string */
        m_current_settings = std::move(json_config_str);

        flatbuffers::GetRoot< typename SettingsT::TableType >(parser.builder_.GetBufferPointer())
            ->UnPackTo(&out_settings, nullptr);
    }

    bool check_restart_needed(const SettingsT* new_settings, const std::shared_ptr< SettingsT > current_settings) {
        flatbuffers::Parser schema_parser;
        if (!schema_parser.Parse(m_raw_schema.c_str())) {
            LOGERROR("Error parsing flatbuffer settings schema: {}", schema_parser.error_);
            return false;
        }
        schema_parser.Serialize();

        const reflection::Schema* schema = reflection::GetSchema(schema_parser.builder_.GetBufferPointer());
        if (schema->root_table() == nullptr) {
            LOGINFO("schema->root_table() is null in check_restart_needed(..)");
            return false;
        }

        /* Create root_obj for new_settings */
        flatbuffers::Parser new_parser;
        if (!new_parser.Parse(m_raw_schema.c_str())) {
            LOGERROR("Error parsing flatbuffer settings schema: {}", new_parser.error_);
            return false;
        }
        new_parser.builder_.Finish(SettingsT::TableType::Pack(new_parser.builder_, new_settings, nullptr));
        auto* root_obj = flatbuffers::GetAnyRoot(new_parser.builder_.GetBufferPointer());

        /* Create root_obj for new_settings */
        flatbuffers::Parser old_parser;
        if (!old_parser.Parse(m_raw_schema.c_str())) {
            LOGERROR("Error parsing flatbuffer settings schema: {}", old_parser.error_);
            return false;
        }
        old_parser.builder_.Finish(SettingsT::TableType::Pack(old_parser.builder_, current_settings.get(), nullptr));
        auto* old_root_obj = flatbuffers::GetAnyRoot(old_parser.builder_.GetBufferPointer());

        /* Call diff */
        bool restart = diff(schema, schema->root_table(), root_obj, old_root_obj);
        if (!restart) {
            LOGINFO("check_restart_needed(..) found no changes which need restart");
        } else {
            LOGINFO("check_restart_needed(..) found changes which need restart");
        }

        return restart;
    }

private:
    std::string m_schema_name;
    std::string m_raw_schema;

    /* Unparsed settings string */
    std::string m_current_settings;

    /* Last settings parse error is stored in this string */
    std::string m_last_error;

    /* RCU protected settings data */
    urcu_data< SettingsT > m_rcu_data;
};

} // namespace sisl

#define WITH_SETTINGS(var, ...) with_settings([](auto& var) __VA_ARGS__)
#define WITH_SETTINGS_CAP1(var, cap1, ...) with_settings([cap1](auto& var) __VA_ARGS__)
#define WITH_SETTINGS_CAP2(var, cap1, cap2, ...) with_settings([ cap1, cap2 ](auto& var) __VA_ARGS__)
#define WITH_SETTINGS_VALUE(path_expr) with_settings([](auto& s_) { return s_.path_expr; })
#define WITH_SETTINGS_THIS(var, ...) with_settings([this](auto& var) __VA_ARGS__)
#define WITH_SETTINGS_THIS_CAP1(var, cap1, ...) with_settings([ this, cap1 ](auto& var) __VA_ARGS__)
#define WITH_SETTINGS_THIS_CAP2(var, cap1, cap2, ...) with_settings([ this, cap1, cap2 ](auto& var) __VA_ARGS__)

// #define SETTINGS_FACTORY(SType) ::sisl::SettingsFactory< SType##T >::instance()

/*
 * SETTINGS(var) invokes user supplied lamdba passing it a safe pointer to an instance of settings object
 * naming it with var
 */
#define SETTINGS(sname, var, ...) SETTINGS_FACTORY(sname).WITH_SETTINGS(var, __VA_ARGS__)
#define SETTINGS_CAP1(sname, var, cap1, ...) SETTINGS_FACTORY(sname).WITH_SETTINGS_CAP1(var, cap1, __VA_ARGS__)
#define SETTINGS_CAP2(sname, var, cap1, cap2, ...)                                                                     \
    SETTINGS_FACTORY(sname).WITH_SETTINGS_CAP2(var, cap1, cap2, __VA_ARGS__)

/*
 * same as above but for lambdas that capture this
 */
#define SETTINGS_THIS(sname, var, ...) SETTINGS_FACTORY(sname).WITH_SETTINGS_THIS(var, __VA_ARGS__)
#define SETTINGS_THIS_CAP1(sname, var, cap1, ...)                                                                      \
    SETTINGS_FACTORY(sname).WITH_SETTINGS_THIS_CAP1(var, cap1, __VA_ARGS__)
#define SETTINGS_THIS_CAP2(sname, var, cap1, cap2, ...)                                                                \
    SETTINGS_FACTORY(sname).WITH_SETTINGS_THIS_CAP2(var, cap1, cap2, __VA_ARGS__)

// #define SETTINGS_VALUE(SType, path_expr) SETTINGS_FACTORY(SType).WITH_SETTINGS_VALUE(path_expr)
#define SETTINGS_VALUE(sname, path_expr) SETTINGS_FACTORY(sname).WITH_SETTINGS_VALUE(path_expr)
