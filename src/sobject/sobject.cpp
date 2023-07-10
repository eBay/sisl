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
    auto iter = m_children.find(name);
    if (iter == m_children.end()) { return nullptr; }
    return iter->second;
}

void sobject::add_child(const sobject_ptr child) {
    // Add a child to current object.
    std::unique_lock lock{m_mtx};
    LOGINFO("Parent {}/{} added child {}/{}", type(), name(), child->type(), child->name());
    m_children.emplace(child->name(), child);
    auto res = m_mgr->add_object_type(type(), child->type());
    if (res) { LOGINFO("Added type parent {} child {}", type(), child->type()); }
}

void sobject::remove_child(const sobject_ptr child) {
    // remove a child from current object.
    std::unique_lock lock{m_mtx};
    auto it = m_children.find(child->name());
    if (it != m_children.end()) {
        LOGINFO("Parent {}/{} removed child {}/{}", type(), name(), child->type(), child->name());
    } else {
        LOGERROR("Parent {}/{} does not have child {}/{}", type(), name(), child->type(), child->name());
        return;
    }
    m_children.erase(child->name());
    auto res = m_mgr->remove_object_type(type(), child->type());
    if (res) { LOGINFO("Removed type parent {} child {}", type(), child->type()); }
}

void sobject::add_child_type(const std::string& child_type) {
    std::unique_lock lock{m_mtx};
    auto res = m_mgr->add_object_type(type(), child_type);
    if (res) { LOGINFO("Added type parent {} child {}", type(), child_type); }
}

void sobject::remove_child_type(const std::string& child_type) {
    std::unique_lock lock{m_mtx};
    bool res = m_mgr->remove_object_type(type(), child_type);
    if (res) { LOGINFO("Removed type parent {} child {}", type(), child_type); }
}

status_response sobject::run_callback(const status_request& request) const {
    status_response response;
    response.json = nlohmann::json::object();
    response.json["type"] = m_type;
    response.json["name"] = m_name;
    auto res = m_status_cb(request).json;
    if (!res.is_null()) { response.json.update(res); }

    for (const auto& [name, obj] : m_children) {
        auto child_type = obj->type();
        auto child_name = obj->name();
        if (response.json["children"] == nullptr) { response.json["children"] = nlohmann::json::object(); }

        if (response.json["children"][child_type] == nullptr) {
            response.json["children"][child_type] == nlohmann::json::array();
        }

        if (request.do_recurse) {
            // Call recursive.
            auto child_json = obj->run_callback(request).json;
            response.json["children"][child_type].emplace_back(child_json);
        } else {
            response.json["children"][child_type].emplace_back(child_name);
        }
    }

    return response;
}

sobject_ptr sobject_manager::create_object(const std::string& type, const std::string& name, status_callback_type cb) {
    std::unique_lock lock{m_mtx};
    auto obj = sobject::create(this, type, name, std::move(cb));
    m_object_store[name] = obj;
    if (m_object_types.count(type) == 0) { m_object_types[type] = {}; }
    LOGINFO("Created status object type={} name={}", type, name);
    return obj;
}

void sobject_manager::remove_object(const std::string& name) {
    std::unique_lock lock{m_mtx};
    m_object_store.erase(name);
    m_object_types[name].clear();
    LOGINFO("Removed status object name={}", name);
}

bool sobject_manager::add_object_type(const std::string& parent_type, const std::string& child_type) {
    bool res = false;
    std::unique_lock lock{m_mtx};
    auto p_map = m_object_types[parent_type].find(child_type);
    if (p_map == m_object_types[parent_type].end()) {
        m_object_types[parent_type][child_type] = 1;
        res = true;
    } else {
        ++m_object_types[parent_type][child_type];
    }
    return res;
}

bool sobject_manager::remove_object_type(const std::string& parent_type, const std::string& child_type) {
    bool res = false;
    std::unique_lock lock{m_mtx};
    auto count = m_object_types[parent_type][child_type];
    if (count <= 1) {
        m_object_types[parent_type].erase(child_type);
        res = true;
    } else {
        --m_object_types[parent_type][child_type];
    }
    return res;
}

status_response sobject_manager::get_object_types(const std::string& type) {
    status_response response;
    auto children = nlohmann::json::object();
    for (const auto& [child, count] : m_object_types[type]) {
        children.emplace(child, get_object_types(child).json);
    }
    response.json = children;
    return response;
}

status_response sobject_manager::get_objects(const status_request& request) {
    status_response response;

    // We by default start from the 'module' types recursively as they are
    // the top of the heirarchy.
    std::string obj_type = request.obj_type.empty() ? "module" : request.obj_type;
    auto iter = m_object_store.begin();
    if (!request.next_cursor.empty()) {
        // Extract cursor which has name.
        iter = m_object_store.find(request.next_cursor);
        if (iter == m_object_store.end()) return status_error("Cursor not found");
    }

    int batch_size = request.batch_size;
    while (iter != m_object_store.end() && batch_size > 0) {
        if (obj_type != iter->second->type()) {
            iter++;
            continue;
        }

        response.json[iter->first] = iter->second->run_callback(request).json;
        iter++;
        batch_size--;
    }

    if (iter != m_object_store.end() && obj_type == iter->second->type()) {
        response.json["next_cursor"] = iter->second->name();
    }

    return response;
}

status_response sobject_manager::get_object_status(const std::string& name, const status_request& request) {
    auto iter = m_object_store.find(name);
    if (iter == m_object_store.end()) { return status_error("Object identifier not found"); }
    return iter->second->run_callback(request);
}

status_response sobject_manager::get_child_type_status(const status_request& request) {
    status_response response;
    auto iter = m_object_store.find(request.obj_name);
    if (iter == m_object_store.end()) { return status_error("Object identifier not found"); }
    for (const auto& [child_name, child_obj] : iter->second->m_children) {
        if (child_obj->type() == request.obj_type) {
            response.json[child_name] = child_obj->run_callback(request).json;
        }
    }
    if (!response.json.empty()) {
        // If we found child in object tree return it.
        return response;
    }

    // Else ask the parent object to do the work. This is used to lazily
    // get objects of type which are not created by default.
    return iter->second->run_callback(request);
}

status_response sobject_manager::get_object_by_path(const status_request& request) {
    sobject_ptr obj = nullptr;
    auto iter = m_object_store.find(request.obj_path[0]);
    if (iter == m_object_store.end()) { return status_error("Object identifier not found"); }
    obj = iter->second;
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

    if (!request.obj_type.empty() && !request.obj_name.empty()) {
        // Get all children under the parent of type.
        return get_child_type_status(request);
    }

    if (!request.obj_name.empty()) {
        // Return specific object.
        return get_object_status(request.obj_name, request);
    }

    if (!request.obj_type.empty()) {
        // Return all objects of this type.
        return get_objects(request);
    }

    if (!request.do_recurse) {
        // If no recurse we only return the types.
        status_response response;
        response.json["module"] = get_object_types("module").json;
        return response;
    }

    // Dump all objects recursively.
    return get_objects(request);
}

} // namespace sisl
