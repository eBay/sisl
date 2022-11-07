/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Author/Developer(s): Harihara Kadayam
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
#include <fstream>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <sisl/logging/logging.h>
#include <sisl/options/options.h>
#include "generated/test_app_schema_generated.h"
#include "settings.hpp"

SISL_OPTIONS_ENABLE(logging, test_settings, config)
SISL_LOGGING_INIT(test_settings, settings)
SETTINGS_INIT(testapp::TestAppSettings, test_app_schema)

SISL_OPTION_GROUP(test_settings,
                  (num_threads, "", "num_threads", "number of threads",
                   ::cxxopts::value< uint32_t >()->default_value("1"), "number"))

static const char* g_schema_file{"/tmp/test_app_schema.json"};

class SettingsTest : public ::testing::Test {
protected:
    void SetUp() override { std::remove(g_schema_file); }

    void init(const std::vector< std::string >& override_cfgs = {}) {
        auto reg_mem = &sisl::SettingsFactoryRegistry::instance();
        sisl::SettingsFactoryRegistry::instance().~SettingsFactoryRegistry();
        new (reg_mem) sisl::SettingsFactoryRegistry("/tmp", override_cfgs);

        auto fac_mem = &test_app_schema_factory::instance();
        sisl::SettingsFactoryRegistry::instance().unregister_factory("test_app_schema");
        test_app_schema_factory::instance().~test_app_schema_factory();
        new (fac_mem) test_app_schema_factory();
    }

    void Teardown() {
        test_app_schema_factory::instance().~test_app_schema_factory();
        sisl::SettingsFactoryRegistry::instance().~SettingsFactoryRegistry();
        std::remove(g_schema_file);
    }
};

TEST_F(SettingsTest, LoadReload) {
    init();

    LOGINFO("Step 1: Validating default load");
    ASSERT_EQ(SETTINGS_VALUE(test_app_schema, config->dbconnection->dbConnectionOptimalLoad), 100UL)
        << "Incorrect load of dbConnectionOptimalLoad - default load";
    SETTINGS(test_app_schema, s, {
        ASSERT_EQ(s.config.database.databaseHost, "") << "Incorrect load of databaseHost - default load";
        ASSERT_EQ(s.config.database.databasePort, 27017u) << "Incorrect load of databasePort - default load";
        ASSERT_EQ(s.config.database.numThreads, 8u) << "Incorrect load of numThreads - default load";
    });
    sisl::SettingsFactoryRegistry::instance().save_all();
    ASSERT_EQ(std::filesystem::exists("/tmp/test_app_schema.json"), true) << "Expect settings save to create the file";
    ASSERT_EQ(sisl::SettingsFactoryRegistry::instance().reload_all(), false)
        << "Incorrectly asking to reload when hotswap variable is changed and reloaded";

    LOGINFO("Step 2: Reload by dumping the settings to json, edit hotswap variable and reload it");
    nlohmann::json j = nlohmann::json::parse(SETTINGS_FACTORY(test_app_schema).get_json());
    j["config"]["dbconnection"]["dbConnectionOptimalLoad"] = 800;
    ASSERT_EQ(SETTINGS_FACTORY(test_app_schema).reload_json(j.dump()), false)
        << "Incorrectly asking restart when hotswap variable is changed and reloaded";
    ASSERT_EQ(SETTINGS_VALUE(test_app_schema, config->dbconnection->dbConnectionOptimalLoad), 800UL)
        << "Incorrect load of dbConnectionOptimalLoad - after reload json";
    SETTINGS(test_app_schema, s, {
        ASSERT_EQ(s.config.database.databaseHost, "") << "Incorrect load of databaseHost - after reload json";
        ASSERT_EQ(s.config.database.databasePort, 27017u) << "Incorrect load of databasePort - after reload json";
        ASSERT_EQ(s.config.database.numThreads, 8u) << "Incorrect load of numThreads - after reload json";
    });

    LOGINFO("Step 3: Reload by dumping the settings to json, edit non-hotswap variable and dump to file and reload "
            "settings");
    j = nlohmann::json::parse(SETTINGS_FACTORY(test_app_schema).get_json());
    j["config"]["database"]["databasePort"] = 25000u;
    {
        std::ofstream file(g_schema_file);
        file << j;
    }
    ASSERT_EQ(sisl::SettingsFactoryRegistry::instance().reload_all(), true)
        << "Incorrectly marking no restart when non-hotswap variable is changed and reloaded";

    LOGINFO("Step 4: Simulate the app restart and validate new values");
    init();
    ASSERT_EQ(SETTINGS_VALUE(test_app_schema, config->dbconnection->dbConnectionOptimalLoad), 800UL)
        << "Incorrect load of dbConnectionOptimalLoad - after restart";
    SETTINGS(test_app_schema, s, {
        ASSERT_EQ(s.config.database.databaseHost, "") << "Incorrect load of databaseHost - after reload json";
        ASSERT_EQ(s.config.database.databasePort, 25000u) << "Incorrect load of databasePort - after reload json";
        ASSERT_EQ(s.config.database.numThreads, 8u) << "Incorrect load of numThreads - after reload json";
    });
    sisl::SettingsFactoryRegistry::instance().save_all();
}

TEST_F(SettingsTest, OverrideConfig) {
    LOGINFO("Step 1: Validating overridden config load");
    init({"test_app_schema.config.database.databaseHost:myhost.com",
          "test_app_schema.config.glog.FLAGS_logbuflevel:100"});
    ASSERT_EQ(SETTINGS_VALUE(test_app_schema, config->database->databaseHost), "myhost.com")
        << "Incorrect load of databaseHost with override config";
    ASSERT_EQ(SETTINGS_VALUE(test_app_schema, config->glog->FLAGS_logbuflevel), 100)
        << "Incorrect load of FLAGS_logbuflevel with override config";
    SETTINGS(test_app_schema, s, {
        ASSERT_EQ(s.config.database.databasePort, 27017u) << "Incorrect load of databasePort - default load";
        ASSERT_EQ(s.config.database.numThreads, 8u) << "Incorrect load of numThreads - default load";
    });
    sisl::SettingsFactoryRegistry::instance().save_all();

    LOGINFO("Step 2: Simulate restart and default load saved the previously overridden config");
    init();
    ASSERT_EQ(SETTINGS_VALUE(test_app_schema, config->database->databaseHost), "myhost.com")
        << "Incorrect load of databaseHost with override config";
    ASSERT_EQ(SETTINGS_VALUE(test_app_schema, config->glog->FLAGS_logbuflevel), 100)
        << "Incorrect load of FLAGS_logbuflevel with override config";
    SETTINGS(test_app_schema, s, {
        ASSERT_EQ(s.config.database.databasePort, 27017u) << "Incorrect load of databasePort - default load";
        ASSERT_EQ(s.config.database.numThreads, 8u) << "Incorrect load of numThreads - default load";
    });
}

#if 0
int main(int argc, char* argv[]) {
    SISL_OPTIONS_LOAD(argc, argv, logging, test_settings, config);
    sisl::logging::SetLogger("test_settings");
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

    // MY_SETTINGS_FACTORY.load_file("/tmp/settings_in.json");

    LOGINFO("After Initial load values are:");
    LOGINFO("dbConnectionOptimalLoad = {} ",
            SETTINGS_VALUE(test_app_schema, config->dbconnection->dbConnectionOptimalLoad));
    MY_SETTINGS_FACTORY.save("/tmp/settings_out");
    SETTINGS(test_app_schema, s, {
        LOGINFO("databaseHost = {}", s.config.database.databaseHost);
        LOGINFO("databasePort = {}", s.config.database.databasePort);
        LOGINFO("numThreads = {}", s.config.database.numThreads);
    });

    LOGINFO("Reload - 1 file: restart needed {}", MY_SETTINGS_FACTORY.reload_file("/tmp/settings_out.json"));

    nlohmann::json j;
    j = nlohmann::json::parse(MY_SETTINGS_FACTORY.get_json());
    j["config"]["dbconnection"]["dbConnectionOptimalLoad"] = 800;
    LOGINFO("Reload - 2 json after changes, restart needed {} ", MY_SETTINGS_FACTORY.reload_json(j.dump()));
    LOGINFO("After reload - 2: values are:");
    LOGINFO("dbConnectionOptimalLoad = {} ",
            SETTINGS_VALUE(test_app_schema, config->dbconnection->dbConnectionOptimalLoad));
    SETTINGS(test_app_schema, s, {
        LOGINFO("databasePort = {}", s.config.database.databasePort);
        LOGINFO("numThreads = {}", s.config.database.numThreads);
    });

    j = nlohmann::json::parse(MY_SETTINGS_FACTORY.get_json());
    j["config"]["database"]["databasePort"] = 25000;
    LOGINFO("Reload - 3 json after changes, restart needed {} ", MY_SETTINGS_FACTORY.reload_json(j.dump()));
    LOGINFO("After reload - 3: values are:");
    LOGINFO("dbConnectionOptimalLoad = {} ",
            SETTINGS_VALUE(test_app_schema, config->dbconnection->dbConnectionOptimalLoad));
    SETTINGS(test_app_schema, s, {
        LOGINFO("databasePort = {}", s.config.database.databasePort);
        LOGINFO("numThreads = {}", s.config.database.numThreads);
    });

    MY_SETTINGS_FACTORY.save("/tmp/settings_out");
    return 0;
}
#endif

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    SISL_OPTIONS_LOAD(argc, argv, logging, test_settings)
    sisl::logging::SetLogger("test_settings");
    spdlog::set_pattern("[%D %T%z] [%^%L%$] [%t] %v");

    auto ret = RUN_ALL_TESTS();
    return ret;
}
