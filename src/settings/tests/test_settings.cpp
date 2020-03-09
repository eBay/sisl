#include "generated/test_app_schema_generated.h"
//#include "generated/test_app_schema_bindump.hpp"
#include "settings/settings.hpp"
#include <sds_logging/logging.h>
#include <sds_options/options.h>
#include <iostream>
#include <nlohmann/json.hpp>

#define MY_SETTINGS_FACTORY SETTINGS_FACTORY(test_app_schema)

SDS_OPTIONS_ENABLE(logging, test_settings)
SDS_OPTION_GROUP(test_settings,
                 (num_threads, "", "num_threads", "number of threads",
                  ::cxxopts::value< uint32_t >()->default_value("1"), "number"))

SDS_LOGGING_INIT(test_settings, settings)
SETTINGS_INIT(testapp::TestAppSettings, test_app_schema);

int main(int argc, char* argv[]) {
    SDS_OPTIONS_LOAD(argc, argv, logging, test_settings);
    sds_logging::SetLogger("test_settings");
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

    // MY_SETTINGS_FACTORY.load_file("/tmp/settings_in.json");

    std::cout << "After Initial load values are: \n";
    std::cout << "Value of dbConnectionOptimalLoad = ? "
              << SETTINGS_VALUE(test_app_schema, config->dbconnection->dbConnectionOptimalLoad) << "\n";
    MY_SETTINGS_FACTORY.save("/tmp/settings_out.json");
    SETTINGS(test_app_schema, s, {
        std::cout << "Value of databasePort = " << s.config.database.databasePort << "\n";
        std::cout << "Value of numThreads = " << s.config.database.numThreads << "\n";
    });

    std::cout << "Reload - 1 file: restart needed ? " << MY_SETTINGS_FACTORY.reload_file("/tmp/settings_in.json")
              << "\n";

    nlohmann::json j;
    j = nlohmann::json::parse(MY_SETTINGS_FACTORY.get_json());
    j["config"]["dbconnection"]["dbConnectionOptimalLoad"] = 800;
    std::cout << "Reload - 2 json after changes, restart needed ? " << MY_SETTINGS_FACTORY.reload_json(j.dump())
              << "\n";
    std::cout << "After reload - 2: values are: \n";
    std::cout << "Value of dbConnectionOptimalLoad = ? "
              << SETTINGS_VALUE(test_app_schema, config->dbconnection->dbConnectionOptimalLoad) << "\n";
    SETTINGS(test_app_schema, s, {
        std::cout << "Value of databasePort = " << s.config.database.databasePort << "\n";
        std::cout << "Value of numThreads = " << s.config.database.numThreads << "\n";
    });

    j = nlohmann::json::parse(MY_SETTINGS_FACTORY.get_json());
    j["config"]["database"]["databasePort"] = 25000;
    std::cout << "Reload - 3 json after changes, restart needed ? " << MY_SETTINGS_FACTORY.reload_json(j.dump())
              << "\n";
    std::cout << "After reload - 3: values are: \n";
    std::cout << "Value of dbConnectionOptimalLoad = ? "
              << SETTINGS_VALUE(test_app_schema, config->dbconnection->dbConnectionOptimalLoad) << "\n";
    SETTINGS(test_app_schema, s, {
        std::cout << "Value of databasePort = " << s.config.database.databasePort << "\n";
        std::cout << "Value of numThreads = " << s.config.database.numThreads << "\n";
    });

    return 0;
}