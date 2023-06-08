/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
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

#include <string>
#include <functional>
#include <map>
#include <set>
#include <memory>
#include <shared_mutex>
#include <vector>
#include <nlohmann/json.hpp>

#include <sisl/logging/logging.h>

namespace sisl {

// Each object is uniquely identified by its type and name.
// Ex: type=volume and name=volume_1, type=module and name=HomeBlks.
struct sobject_id {
    std::string type;
    std::string name;
    bool empty() const { return type.empty() && name.empty(); }
    [[maybe_unused]] bool operator<(const sobject_id& id) const {
        return type < id.type || ((type == id.type) && (name < id.name));
    }
};

typedef struct status_request {
    nlohmann::json json;
    bool do_recurse{false};
    int verbose_level = 0;
    std::string obj_type;
    std::string obj_name;
    std::vector< std::string > obj_path;
    int batch_size = 10;
    std::string next_cursor;
} status_request;

typedef struct status_response {
    nlohmann::json json;
} status_response;

using status_callback_type = std::function< status_response(const status_request&) >;
class sobject;
class sobject_manager;
using sobject_ptr = std::shared_ptr< sobject >;

// To search using only the type as key.
[[maybe_unused]] static bool operator<(const sobject_id& id, const std::string& key_type) { return id.type < key_type; }

[[maybe_unused]] static bool operator<(const std::string& key_type, const sobject_id& id) { return key_type < id.type; }

[[maybe_unused]] static status_response status_error(std::string error_str) {
    status_response response;
    response.json["error"] = error_str;
    return response;
}

// Similar to sysfs kobject, sobject is a lightweight utility to create relationships
// between different classes and modules. This can be used to get or change the state of a class
// and all its children. Modules/subsystems which register their callbacks to be
// whenever a get status is called from the root or directly.
class sobject {
public:
    sobject(sobject_manager* mgr, const std::string& obj_type, const std::string& obj_name, status_callback_type cb) :
            m_mgr(mgr), m_id{obj_type, obj_name}, m_status_cb(std::move(cb)) {}

    static sobject_ptr create(sobject_manager* mgr, const std::string& obj_type, const std::string& obj_name,
                              status_callback_type cb) {
        return std::make_shared< sobject >(mgr, obj_type, obj_name, std::move(cb));
    }

    // Every subsystem add to the json object using update().
    status_response run_callback(const status_request& request) const;
    sobject_ptr get_child(const std::string& name);
    void add_child(const sobject_ptr child);

    sobject_id id() const { return m_id; }
    std::string name() const { return m_id.name; }
    std::string type() const { return m_id.type; }

private:
    sobject_manager* m_mgr;
    sobject_id m_id;
    std::shared_mutex m_mtx;
    status_callback_type m_status_cb;
    // Keep a graph of child nodes. Mapping from name to child status object.
    std::map< sobject_id, sobject_ptr > m_children;
};

class sobject_manager {
public:
    sobject_ptr create_object(const std::string& type, const std::string& name, status_callback_type cb);
    status_response get_status(const status_request& request);

    status_response get_object_by_path(const status_request& request);
    status_response get_object_status(const sobject_id& id, const status_request& request);
    status_response get_objects(const status_request& request);
    status_response get_object_types();
    void add_object_type(const std::string& parent_type, const std::string& child_type);

private:
    // Mapping from object name to object metadata.
    std::map< sobject_id, sobject_ptr, std::less<> > m_object_store;
    // Mapping from parent type to set of all children type to display the schema.
    std::map< std::string, std::set< std::string > > m_object_types;
    std::shared_mutex m_mtx;
};

} // namespace sisl
