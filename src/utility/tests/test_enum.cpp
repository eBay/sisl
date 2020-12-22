//
// Created by Zimmerman, Bryan on 2020/12/21
//

#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>

#include "utility/enum.hpp"

class EnumTest : public testing::Test {
public:
    EnumTest() = default;
    EnumTest(const EnumTest&) = delete;
    EnumTest(EnumTest&&) noexcept = delete;
    EnumTest& operator=(const EnumTest&) = delete;
    EnumTest& operator=(EnumTest&&) noexcept = delete;

    virtual ~EnumTest() override = default;

protected:
    void SetUp() override {}
    void TearDown() override {}
};

ENUM(signed_enum, int16_t, val1, val2)
TEST_F(EnumTest, enum_signed_test) {
    auto enum_lambda{[](const signed_enum& val) {
        switch (val) {
        case signed_enum::val1:
            return 1;
        case signed_enum::val2:
            return 2;
        default:
            return 0;
        };
    }};

    ASSERT_EQ(enum_lambda(signed_enum::val1), 1);
    ASSERT_EQ(enum_lambda(signed_enum::val2), 2);
}

ENUM(unsigned_enum, uint16_t, val1, val2)
TEST_F(EnumTest, enum_unsigned_test) {
    auto enum_lambda{[](const unsigned_enum& val) {
        switch (val) {
        case unsigned_enum::val1:
            return 1;
        case unsigned_enum::val2:
            return 2;
        default:
            return 0;
        };
    }};

    ASSERT_EQ(enum_lambda(unsigned_enum::val1), 1);
    ASSERT_EQ(enum_lambda(unsigned_enum::val2), 2);
}

ENUM(signed_enum_value, int16_t, val1=-10, val2=-20)
TEST_F(EnumTest, enum_signed_value_test) {
    auto enum_lambda{[](const signed_enum_value& val) {
        switch (val) {
        case signed_enum_value::val1:
            return static_cast< std::underlying_type_t< signed_enum_value > >(signed_enum_value::val1);
        case signed_enum_value::val2:
            return static_cast< std::underlying_type_t< signed_enum_value > >(signed_enum_value::val2);
        default:
            return std::underlying_type_t< signed_enum_value >{};
        };
    }};

    ASSERT_EQ(enum_lambda(signed_enum_value::val1), -10);
    ASSERT_EQ(enum_lambda(signed_enum_value::val2), -20);
}

ENUM(unsigned_enum_value, uint16_t, val1=10, val2=20)
TEST_F(EnumTest, enum_unsigned_value_test) {
    auto enum_lambda{[](const unsigned_enum_value& val) {
        switch (val) {
        case unsigned_enum_value::val1:
            return static_cast< std::underlying_type_t< unsigned_enum_value > >(unsigned_enum_value::val1);
        case unsigned_enum_value::val2:
            return static_cast< std::underlying_type_t< unsigned_enum_value > >(unsigned_enum_value::val2);
        default:
            return std::underlying_type_t< unsigned_enum_value >{};
        };
    }};

    ASSERT_EQ(enum_lambda(unsigned_enum_value::val1), 10);
    ASSERT_EQ(enum_lambda(unsigned_enum_value::val2), 20);
}


int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}