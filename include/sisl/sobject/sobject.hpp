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

[[maybe_unused]] static status_response status_error(std::string error_str) {
    status_response response;
    response.json["error"] = error_str;
    return response;
}

// Similar to sysfs kobject, sobject is a lightweight utility to create relationships
// between different classes and modules. This can be used to get or change the state of a class
// and all its children. Modules/subsystems which register their callbacks to be
// whenever a get status is called from the root or directly. Object is uniquely identified by its name.
class sobject {
public:
    sobject(sobject_manager* mgr, const std::string& obj_type, const std::string& obj_name, status_callback_type cb) :
            m_mgr(mgr), m_type(obj_type), m_name(obj_name), m_status_cb(std::move(cb)) {}

    static sobject_ptr create(sobject_manager* mgr, const std::string& obj_type, const std::string& obj_name,
                              status_callback_type cb) {
        return std::make_shared< sobject >(mgr, obj_type, obj_name, std::move(cb));
    }

    // Every subsystem add to the json object using update().
    status_response run_callback(const status_request& request) const;
    sobject_ptr get_child(const std::string& name);
    void add_child(const sobject_ptr child);
    void add_child_type(const std::string& child_type);

    std::string name() const { return m_name; }
    std::string type() const { return m_type; }

private:
    sobject_manager* m_mgr;
    std::string m_type;
    std::string m_name;
    std::shared_mutex m_mtx;
    status_callback_type m_status_cb;
    // Keep a graph of child nodes. Mapping from name to child status object.
    std::map< std::string, sobject_ptr > m_children;
    friend class sobject_manager;
};

class sobject_manager {
private:
public:
    sobject_ptr create_object(const std::string& type, const std::string& name, status_callback_type cb);
    status_response get_status(const status_request& request);

    status_response get_child_type_status( const status_request& request);
    status_response get_object_by_path(const status_request& request);
    status_response get_object_status(const std::string& name, const status_request& request);
    status_response get_objects(const status_request& request);
    status_response get_object_types(const std::string& type);
    void add_object_type(const std::string& parent_type, const std::string& child_type);

private:
    // Mapping from object name to object metadata. Object names are required
    // to be unique.
    std::map< std::string, sobject_ptr, std::less<> > m_object_store;
    // Mapping from parent type to set of all children type to display the schema.
    std::map< std::string, std::set< std::string > > m_object_types;
    std::shared_mutex m_mtx;
};

} // namespace sisl
