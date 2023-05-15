/*********************************************************************************
 * Modifications Copyright 2023 eBay Inc.
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
#include <cstdint>
#include <memory>
#include <random>
#include <vector>
#include <map>

#include <sisl/logging/logging.h>
#include <sisl/options/options.h>

#include <gtest/gtest.h>
#include "sisl/sobject/sobject.hpp"

using namespace sisl;
using namespace std;

SISL_LOGGING_INIT(test_sobject)
SISL_OPTIONS_ENABLE(logging)

namespace {

class SobjectTest : public testing::Test {
public:
    SobjectTest() : testing::Test() {}

    sobject_manager mgr;

protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(SobjectTest, BasicTest) {

    auto create_nodes = [this](sobject_ptr parent, string type, string prefix, int count) {
        vector< sobject_ptr > res;
        for (int i = 1; i <= count; i++) {
            auto n = prefix + to_string(i);
            auto cb = [n](const status_request&) {
                status_response resp;
                resp.json[n + "_metric"] = 1;
                return resp;
            };

            auto o = mgr.create_object(type, n, cb);
            res.push_back(o);
            if (parent) { parent->add_child(o); }
        }
        return res;
    };

    // Create heirarchy of objects.
    auto a_vec = create_nodes(nullptr, "A", "A", 2);
    auto a_child_vec = create_nodes(a_vec[0], "A_child", "A_child", 2);
    auto b_vec = create_nodes(nullptr, "B", "B", 2);
    auto b_child_vec = create_nodes(b_vec[0], "B", "BB", 2);
    auto c_vec = create_nodes(nullptr, "C", "C", 2);
    auto c_child_vec = create_nodes(c_vec[0], "C_child", "C_child", 2);
    auto c_child_child_vec = create_nodes(c_child_vec[0], "C_child_child", "C_child_child", 2);

    {
        status_request req;
        status_response resp;
        resp = mgr.get_status(req);
        ASSERT_EQ(resp.json["types"].size(), 6) << resp.json.dump(2);

        req.do_recurse = true;
        req.batch_size = 100;
        resp = mgr.get_status(req);
        ASSERT_EQ(resp.json.size(), 14) << resp.json.dump(2);
    }

    {
        status_request req;
        status_response resp;
        req.obj_type = "B";
        req.obj_name = "B1";
        req.do_recurse = true;
        resp = mgr.get_status(req);
        ASSERT_EQ(resp.json["children"]["B"]["BB1"]["name"], "BB1") << resp.json.dump(2);

        req.do_recurse = false;
        resp = mgr.get_status(req);
        ASSERT_EQ(resp.json["children"]["B"].size(), 2) << resp.json.dump(2);
        ASSERT_EQ(resp.json["children"]["B"][0], "BB1") << resp.json.dump(2);
    }
    {
        status_request req;
        status_response resp;
        req.do_recurse = true;
        req.obj_type = "C";
        resp = mgr.get_status(req);
        ASSERT_EQ(resp.json.size(), 2) << resp.json.dump(2);
    }

    {
        status_request req;
        status_response resp;
        req.obj_type = "C_child_child";
        req.obj_name = "C_child_child2";
        resp = mgr.get_status(req);
        LOGINFO("Response {}", resp.json.dump(2));
        ASSERT_EQ(resp.json["name"], "C_child_child2") << resp.json.dump(2);
        ASSERT_EQ(resp.json["type"], "C_child_child") << resp.json.dump(2);
    }

    {
        status_request req;
        status_response resp;
        req.obj_path = {"C1", "C_child1", "C_child_child1"};
        req.do_recurse = false;
        resp = mgr.get_status(req);
        LOGINFO("Response {}", resp.json.dump(2));
        ASSERT_EQ(resp.json["name"], "C_child_child1") << resp.json.dump(2);
        ASSERT_EQ(resp.json["type"], "C_child_child") << resp.json.dump(2);
    }

    {
        status_request req;
        status_response resp;
        auto d_vec = create_nodes(nullptr, "D", "D", 10);
        req.do_recurse = true;
        req.batch_size = 1;
        req.obj_type = "D";
        auto count = 10;
        while (true) {
            resp = mgr.get_status(req);
            count--;
            LOGINFO("Response {}", resp.json.dump(2));
            if (!resp.json.contains("next_cursor")) break;
            req.next_cursor = resp.json["next_cursor"];
        }

        ASSERT_EQ(count, 0) << resp.json.dump(2);
    }
}
} // namespace

int main(int argc, char* argv[]) {
    SISL_OPTIONS_LOAD(argc, argv, logging)
    ::testing::InitGoogleTest(&argc, argv);
    sisl::logging::SetLogger("test_sobject");
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

    const auto ret{RUN_ALL_TESTS()};
    return ret;
}
