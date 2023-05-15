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
#include <boost/algorithm/string.hpp>
#include <mutex>
#include "sisl/logging/logging.h"
#include "sisl/sobject/sobject.hpp"

namespace sisl {

sobject_ptr sobject::get_child(const std::string& name) {
    std::shared_lock lock{m_mtx};
    for (const auto& [id, obj] : m_children) {
        if (id.name == name) { return obj; }
    }
    return nullptr;
}

// Add a child to current object.
void sobject::add_child(const sobject_ptr child) {
    std::unique_lock lock{m_mtx};
    LOGINFO("Parent {}/{} added child {}/{}", type(), name(), child->type(), child->name());
    m_children.emplace(child->id(), child);
}

status_response sobject::run_callback(const status_request& request) const {
    status_response response;
    response.json = nlohmann::json::object();
    response.json["type"] = m_id.type;
    response.json["name"] = m_id.name;
    response.json.update(m_status_cb(request).json);
    response.json["children"] = nlohmann::json::object();

    for (const auto& [id, obj] : m_children) {
        if (response.json["children"][id.type] == nullptr) {
            if (request.do_recurse) {
                response.json["children"][id.type] == nlohmann::json::object();
            } else {
                response.json["children"][id.type] == nlohmann::json::array();
            }
        }

        if (request.do_recurse) {
            // Call recursive.
            auto child_json = obj->run_callback(request).json;
            response.json["children"][id.type].emplace(id.name, child_json);
        } else {
            response.json["children"][id.type].push_back(id.name);
        }
    }

    return response;
}

sobject_ptr sobject_manager::create_object(const std::string& type, const std::string& name, status_callback_type cb) {
    std::unique_lock lock{m_mtx};
    auto obj = sobject::create(type, name, std::move(cb));
    sobject_id id{type, name};
    m_object_store[id] = obj;
    m_object_types.insert(type);
    LOGINFO("Created status object type={} name={}", type, name);
    return obj;
}

status_response sobject_manager::get_object_types() {
    status_response response;
    auto types = nlohmann::json::array();
    for (const auto& type : m_object_types) {
        types.emplace_back(type);
    }

    response.json["types"] = std::move(types);
    return response;
}

status_response sobject_manager::get_objects(const status_request& request) {
    status_response response;

    auto iter = m_object_store.begin();
    if (!request.next_cursor.empty()) {
        // Extract cursor which is of format "type:name"
        auto index = request.next_cursor.find_first_of("^");
        if (index == std::string::npos) return status_error("Invalid cursor");
        auto type = request.next_cursor.substr(0, index);
        auto name = request.next_cursor.substr(index + 1);
        iter = m_object_store.find(sobject_id{type, name});
        if (iter == m_object_store.end()) return status_error("Cursor not found");
    } else if (request.obj_name.empty() && !request.obj_type.empty()) {
        // Get all objects of type requested.
        iter = m_object_store.find(request.obj_type);
    }

    int batch_size = request.batch_size;
    while (iter != m_object_store.end() && batch_size > 0) {
        if (request.obj_name.empty() && !request.obj_type.empty() && request.obj_type != iter->first.type) {
            // If only one type of objects requested.
            return response;
        }

        response.json[iter->first.name] = iter->second->run_callback(request).json;
        iter++;
        batch_size--;
    }

    if (iter != m_object_store.end()) { response.json["next_cursor"] = iter->first.type + "^" + iter->first.name; }

    return response;
}

status_response sobject_manager::get_object_status(const sobject_id& id, const status_request& request) {
    auto iter = m_object_store.find(id);
    if (iter == m_object_store.end()) { return status_error("Object identifier not found"); }
    return iter->second->run_callback(request);
}

status_response sobject_manager::get_object_by_path(const status_request& request) {
    sobject_ptr obj = nullptr;
    for (const auto& [id, obj_ptr] : m_object_store) {
        if (id.name == request.obj_path[0]) {
            obj = obj_ptr;
            break;
        }
    }

    if (obj == nullptr) { return status_error("Object identifier not found"); }
    for (uint32_t ii = 1; ii < request.obj_path.size(); ii++) {
        obj = obj->get_child(request.obj_path[ii]);
        if (obj == nullptr) { return status_error("Object identifier not found"); }
    }
    return obj->run_callback(request);
}

status_response sobject_manager::get_status(const status_request& request) {
    std::shared_lock lock{m_mtx};

    if (!request.obj_path.empty()) {
        // Return object status by path.
        return get_object_by_path(request);
    }

    // If both are empty, we return all the types. If both not empty, we return the specific object.
    // Its an error to have name non empty and type empty.
    if (!request.obj_name.empty() && request.obj_type.empty()) { return status_error("Type details not given"); }

    if (!request.obj_name.empty() && !request.obj_type.empty()) {
        // Return specific object.
        sobject_id id{request.obj_type, request.obj_name};
        return get_object_status(std::move(id), request);
    }

    if (!request.do_recurse && request.obj_name.empty() && request.obj_type.empty()) {
        return get_object_types();
    }

    // Dump all objects.
    return get_objects(request);
}

} // namespace sisl
