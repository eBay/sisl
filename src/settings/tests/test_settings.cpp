#include <cstdint>
#include <iostream>

#include <nlohmann/json.hpp>
#include "logging/logging.h"
#include <sds_options/options.h>

#include "generated/test_app_schema_generated.h"
//#include "generated/test_app_schema_bindump.hpp"
#include "settings/settings.hpp"

#define MY_SETTINGS_FACTORY SETTINGS_FACTORY(test_app_schema)

SDS_OPTIONS_ENABLE(logging, test_settings, config)

SDS_OPTION_GROUP(test_settings,
                 (num_threads, "", "num_threads", "number of threads",
                  ::cxxopts::value< uint32_t >()->default_value("1"), "number"))

SISL_LOGGING_INIT(test_settings, settings)
SETTINGS_INIT(testapp::TestAppSettings, test_app_schema)

int main(int argc, char* argv[]) {
    SDS_OPTIONS_LOAD(argc, argv, logging, test_settings, config);
    sisl_logging::SetLogger("test_settings");
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
