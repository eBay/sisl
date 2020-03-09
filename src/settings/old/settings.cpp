#include "settings.hpp"
#include <sds_logging/logging.h>
#include <utility/urcu_helper.hpp>

#include <boost/algorithm/string.hpp>
#include <flatbuffers/idl.h>
#include <ctime>
#include <fstream>
#include <boost/filesystem.hpp>
#include <boost/filesystem/operations.hpp>
//#include <core/lib/debug/trace.h>

#if 0
/* declaring here since diff_vector calls diff function */
static bool diff(const reflection::Schema* schema, const reflection::Object* schema_object,
                 const flatbuffers::Table* root, const flatbuffers::Table* old_root);

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
                CVLOG(VMODULE_SETTINGS, 1)
                    << field->name()->str() << ", idx: " << idx << " changed, old: " << oldval << ", new: " << newval;
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
                CVLOG(VMODULE_SETTINGS, 1)
                    << field->name()->str() << ", idx: " << idx << " changed, old: " << oldval << ", new: " << newval;
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
                CVLOG(VMODULE_SETTINGS, 1)
                    << field->name()->str() << ", idx: " << idx << " changed, old: " << oldval << ", new: " << newval;
                return true;
            }
        }
        break;
    }

    case reflection::BaseType::Vector: {
        LOG_ASSERT(false) << "Nested vector not supported as of flatbuffer 1.9.0";
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

    case reflection::BaseType::Union: {
        // Please do not use unions in settings. It's crazy!
        LOG_ASSERT(false) << "reflection::BaseType::Union type in settings is not supported";
        break;
    }
    }
    return false;
}

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
                CVLOG(VMODULE_SETTINGS, 1) << field->name()->str() << " changed, old: " << a1 << ", new: " << a2;
                return true;
            }
            break;
        }

        case reflection::BaseType::Float:
        case reflection::BaseType::Double: {
            auto a1 = flatbuffers::GetAnyFieldF(*old_root, *field);
            auto a2 = flatbuffers::GetAnyFieldF(*root, *field);
            if (a1 != a2) {
                CVLOG(VMODULE_SETTINGS, 1) << field->name()->str() << " changed, old: " << a1 << ", new: " << a2;
                return true;
            }
            break;
        }

        case reflection::BaseType::String: {
            auto s1 = flatbuffers::GetFieldS(*old_root, *field);
            auto s2 = flatbuffers::GetFieldS(*root, *field);
            if (s1 != nullptr && s2 != nullptr && s1->str() != s2->str()) {
                CVLOG(VMODULE_SETTINGS, 1) << field->name()->str() << " changed, old: " << s1 << ", new: " << s2;
                return true;
            }
            break;
        }
        case reflection::BaseType::Vector: {
            auto v1 = flatbuffers::GetFieldAnyV(*old_root, *field);
            auto v2 = flatbuffers::GetFieldAnyV(*root, *field);
            auto restart = diff_vector(schema, field, v1, v2);
            if (restart) {
                CVLOG(VMODULE_SETTINGS, 1)
                    << "Field: " << field->name()->str() << ", vector has different old and new values";
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

        case reflection::BaseType::Union: {
            // Please do not use unions in settings. It's crazy!
            LOG_ASSERT(false) << "reflection::BaseType::Union type in settings is not supported";
            break;
        }
        }
    }
    return false;
}
#endif

namespace sisl {
#if 0
template < typename SettingsT >
static bool check_restart_needed(const SettingsT* new_settings, const SettingsT* current_settings) {
    flatbuffers::Parser schema_parser;
    if (!schema_parser.Parse(m_raw_schema.c_str())) {
        LOG(ERROR) << "Error parsing flatbuffer settings schema: " << schema_parser.error_;
        return false;
    }
    schema_parser.Serialize();

    const reflection::Schema* schema = reflection::GetSchema(schema_parser.builder_.GetBufferPointer());
    if (schema->root_table() == nullptr) {
        LOG(INFO) << "schema->root_table() is null in check_restart_needed(..)";
        return false;
    }

    /* Create root_obj for new_settings */
    flatbuffers::Parser new_parser;
    if (!new_parser.Parse(m_raw_schema)) {
        LOG(ERROR) << "Error parsing flatbuffer settings schema: " << new_parser.error_;
        return false;
    }
    new_parser.builder_.Finish(SettingsT::Pack(new_parser.builder_, new_settings, nullptr));
    auto* root_obj = flatbuffers::GetAnyRoot(new_parser.builder_.GetBufferPointer());

    /* Create root_obj for new_settings */
    flatbuffers::Parser old_parser;
    if (!old_parser.Parse(m_raw_schema)) {
        LOG(ERROR) << "Error parsing flatbuffer settings schema: " << old_parser.error_;
        return false;
    }
    old_parser.builder_.Finish(MonstorDatabaseSettings::Pack(old_parser.builder_, current_settings, nullptr));
    auto* old_root_obj = flatbuffers::GetAnyRoot(old_parser.builder_.GetBufferPointer());

    /* Call diff */
    bool restart = diff(schema, schema->root_table(), root_obj, old_root_obj);
    if (!restart) {
        LOG(INFO) << "check_restart_needed(..) found no changes which need restart";
    } else {
        LOG(INFO) << "check_restart_needed(..) found changes which need restart";
    }

    return restart;
}
#endif

template < typename SettingsT >
SettingsT SettingsFactory< SettingsT >::parse_config() {
    std::string json_config_str;
    if (!flatbuffers::LoadFile(m_settings_file_path.c_str(), false, &json_config_str)) {
        m_last_error = "flatbuffer::LoadFile() returned false";
        throw std::invalid_argument(m_last_error);
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

    SettingsT settings;
    flatbuffers::GetRoot< SettingsT::NativeTableType >(parser.builder_.GetBufferPointer())
        ->UnPackTo(&settings, nullptr);

    return settings;
}

template < typename SettingsT >
void SettingsFactory< SettingsT >::load(const std::string& config_file_path) {
    m_settings_file_path = config_file_path;
    try {
        auto new_settings = parse_config();
        // post_process(true, &new_settings);
        m_rcu_data.move_and_exchange(new_settings);
    } catch (std::exception& e) {
        std::stringstream ss;
        ss << "Exception reading config " << get_local_config_file() << "(errmsg = " << e.what() << ") ";
        throw std::runtime_error(ss.str());
    }
}

#if 0
bool MonstorSettingsFactory::reload() {
    try {
        auto new_settings = parse_config(this);
        /* post_process may reconfigure some settings, therefore this has to be called before taking diff */
        post_process(false, &new_settings);

        if (check_restart_needed(&new_settings, m_rcu_data.get_node()->get())) {
            m_current_settings = ""; /* getSettings will return empty briefly before exiting */
            return true;
        } else {
            m_rcu_data.move_and_exchange(new_settings);
        }

        if (new_settings.config->dataPathAuthorizationEnabled) {
            METRICS_FACTORY.updateMonstorSercurityRotationSecretGauge(SETTINGS_PARAM(processed->authDataPath->md5str));
        }
    } catch (std::exception& e) {
        LOG(ERROR) << "Exception reading config " << get_local_config_file() << "(errmsg = " << e.what() << ") ";
    }
    return false;
}
#endif

template < typename SettingsT >
const std::string SettingsFactory< SettingsT >::get_json() const {
    std::string json;
    flatbuffers::Parser parser;
    parser.opts.strict_json = true;
    parser.opts.output_default_scalars_in_json = true;

    if (!parser.Parse(m_raw_schema.c_str())) { return "Error parsing flatbuffer settings schema"; }

    parser.builder_.Finish(SettingsT::NativeTableType::Pack(parser.builder_, m_rcu_data.get_node()->get(), nullptr));
    if (!GenerateText(parser, parser.builder_.GetBufferPointer(), &json)) {
        return "Error generating json from flatbuffer";
    }
    return json;
}

#if 0
template < typename SettingsT >
const std::string& SettingsFactory::get_version() const {
    static std::string g_version([]() {
        // set version information
        std::string version{getVersion()};
        std::ifstream in("version");
        if (in) {
            std::string tmp;
            in >> tmp;
            version += "(" + tmp + ")";
        }
        return version;
    }());
    return g_version;
}
#endif

template < typename SettingsT >
void SettingsFactory< SettingsT >::save(const std::string& filepath) {
    std::string json;
    flatbuffers::Parser parser;
    parser.opts.strict_json = true;

    if (!parser.Parse(m_raw_schema.c_str())) { return; }

    parser.builder_.Finish(SettingsT::NativeTableType::Pack(parser.builder_, m_rcu_data.get_node()->get(), nullptr));
    if (!GenerateTextFile(parser, "", filepath)) { return; }
}
} // namespace sisl