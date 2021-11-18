#include <cstdint>
#include <iterator>
#include <memory>
#include <thread>
#include <vector>
#include <iostream>
#include <cstdlib>

#include <gtest/gtest.h>
#include "logging/logging.h"
#include <sds_options/options.h>

#include "fds/buffer.hpp"
#include "obj_life_counter.hpp"

SISL_LOGGING_INIT(test_objlife)

template < typename T1, typename T2 >
struct TestClass : sisl::ObjLifeCounter< TestClass< T1, T2 > > {
    TestClass() : m_x{rand()} {}

private:
    int m_x;
};

struct ObjLifeTest : public testing::Test {
public:
    ObjLifeTest() : testing::Test{} {}
    ObjLifeTest(const ObjLifeTest&) = delete;
    ObjLifeTest(ObjLifeTest&&) noexcept = delete;
    ObjLifeTest& operator=(const ObjLifeTest&) = delete;
    ObjLifeTest& operator=(ObjLifeTest&&) noexcept = delete;
    virtual ~ObjLifeTest() override = default;
};

TEST_F(ObjLifeTest, BasicCount) {
    TestClass< char*, unsigned int > i1;
    TestClass< double, sisl::blob > d1;

    sisl::ObjCounterRegistry::enable_metrics_reporting();
    {
        auto ip2 = std::make_unique< TestClass< char*, unsigned int > >();
        sisl::ObjCounterRegistry::foreach ([this](const std::string& name, int64_t created, int64_t alive) {
            if (name == "TestClass<char*, unsigned int>") {
                ASSERT_EQ(created, 2);
                ASSERT_EQ(alive, 2);
            } else if (name == "TestClass<double, sisl::blob>") {
                ASSERT_EQ(created, 1);
                ASSERT_EQ(created, 1);
            } else {
                ASSERT_TRUE(false);
            }
        });
    }

    sisl::ObjCounterRegistry::foreach ([this](const std::string& name, int64_t created, int64_t alive) {
        if (name == "TestClass<char*, unsigned int>") {
            ASSERT_EQ(created, 2);
            ASSERT_EQ(alive, 1);
        } else if (name == "TestClass<double, sisl::blob>") {
            ASSERT_EQ(created, 1);
            ASSERT_EQ(created, 1);
        } else {
            ASSERT_TRUE(false);
        }
    });

    const nlohmann::json j{sisl::MetricsFarm::getInstance().get_result_in_json()};
    std::cout << "Json output = " << j.dump(2);

    const auto prom_format{sisl::MetricsFarm::getInstance().report(sisl::ReportFormat::kTextFormat)};
    std::cout << "Prometheus Output = " << prom_format;
    ASSERT_TRUE(prom_format.find(R"(TestClass_double__sisl::blob_{entity="Singleton",type="alive"} 1.0)") !=
                std::string::npos);
    ASSERT_TRUE(prom_format.find(R"(TestClass_double__sisl::blob_{entity="Singleton",type="created"} 1.0)") !=
                std::string::npos);
    ASSERT_TRUE(prom_format.find(R"(TestClass_charP__unsigned_int_{entity="Singleton",type="alive"} 1.0)") !=
                std::string::npos);
    ASSERT_TRUE(prom_format.find(R"(TestClass_charP__unsigned_int_{entity="Singleton",type="created"} 2.0)") !=
                std::string::npos);
}

uint32_t g_num_threads;
SDS_OPTIONS_ENABLE(logging, test_objlife)
SDS_OPTION_GROUP(test_objlife,
                 (num_threads, "", "num_threads", "number of threads",
                  ::cxxopts::value< uint32_t >()->default_value("8"), "number"))

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    SDS_OPTIONS_LOAD(argc, argv, logging, test_objlife);
    sisl_logging::SetLogger("test_objlife");
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

    g_num_threads = SDS_OPTIONS["num_threads"].as< uint32_t >();

#ifdef _PRERELEASE
    const auto ret{RUN_ALL_TESTS()};
    return ret;
#else
    return 0;
#endif
}
