/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Author/Developer(s): Yuri Finkelstein, Harihara Kadayam, Bryan Zimmerman
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on  * an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 *
 *********************************************************************************/
#ifndef SISL_ENUM_HPP
#define SISL_ENUM_HPP

#include <cstdlib>
#include <iostream>
#include <iterator>
#include <ostream>
#include <regex>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>

template < typename EnumType >
class EnumSupportBase {
public:
    typedef EnumType enum_type;
    typedef std::underlying_type_t< enum_type > underlying_type;
    static_assert(std::is_enum_v< enum_type >, "Type must be an enum type.");
    EnumSupportBase(const std::string& tokens_string) {
        underlying_type last_value{};
        size_t current_pos{0};
        auto trim{[](const std::string& str, const std::string& whitespace = " \t") -> std::string {
            const size_t str_begin{str.find_first_not_of(whitespace)};
            if (str_begin == std::string::npos) return ""; // no content

            const size_t str_end{str.find_last_not_of(whitespace)};
            return str.substr(str_begin, str_end - str_begin + 1);
        }};
        auto evaluate{[&trim](const std::string& str) {
            const size_t lshift_pos{str.find("<<", 0)};
            if (lshift_pos != std::string::npos) {
                // evaluate left shift
                size_t idx1{}, idx2{};
                const auto lhs{std::stoull(trim(str.substr(0, lshift_pos)), &idx1, 0)};
                const auto rhs{std::stoull(trim(str.substr(lshift_pos + 2)), &idx2, 0)};
                const auto val{lhs << rhs};
                return std::to_string(val);
            }
            // maybe in future provide evaluator for simple expressions
            return str;
        }};
        while (current_pos < tokens_string.size()) {
            const size_t delim{tokens_string.find_first_of(",=", current_pos)};
            const std::string token{trim(tokens_string.substr(current_pos, delim - current_pos))};
            if ((delim != std::string::npos) && (tokens_string[delim] == '=')) {
                const size_t comma_pos{tokens_string.find(',', delim + 1)};
                const size_t length{comma_pos != std::string::npos ? comma_pos - delim : comma_pos};
                const std::string val_string{evaluate(trim(tokens_string.substr(delim + 1, length)))};
                size_t idx{0};
                if constexpr (std::is_unsigned_v< underlying_type >) {
                    last_value = static_cast< underlying_type >(std::stoull(val_string, &idx, 0));
                } else {
                    last_value = static_cast< underlying_type >(std::stoll(val_string, &idx, 0));
                }
                current_pos = ((comma_pos != std::string::npos) ? comma_pos + 1 : comma_pos);
            } else {
                if (m_value_to_tokens.size() != 0) ++last_value;
                current_pos = ((delim != std::string::npos) ? delim + 1 : delim);
            }
            // std::cout << token << ' ' << last_value << std::endl;
            m_value_to_tokens[last_value] = token;
            m_token_to_value[token] = last_value;
        }
    }
    EnumSupportBase(const EnumSupportBase&) = delete;
    EnumSupportBase(EnumSupportBase&&) noexcept = delete;
    EnumSupportBase& operator=(const EnumSupportBase&) = delete;
    EnumSupportBase& operator=(EnumSupportBase&&) noexcept = delete;
    ~EnumSupportBase() = default;

    [[nodiscard]] const std::string& get_name(const enum_type enum_value) const {
        static const std::string unknown{"???"};
        const underlying_type val{static_cast< underlying_type >(enum_value)};
        const auto itr{m_value_to_tokens.find(val)};
        if (itr == std::cend(m_value_to_tokens))
            return unknown;
        else
            return itr->second;
    }

    [[nodiscard]] enum_type get_enum(const std::string& name) const {
        const auto itr{m_token_to_value.find(name)};
        if (itr == std::cend(m_token_to_value))
            return static_cast< enum_type >(0);
        else
            return static_cast< enum_type >(itr->second);
    }

private:
    std::unordered_map< underlying_type, std::string > m_value_to_tokens;
    std::unordered_map< std::string, underlying_type > m_token_to_value;
};

#define VENUM(EnumName, Underlying, ...) ENUM(EnumName, Underlying, __VA_ARGS__)

#define ENUM(EnumName, Underlying, ...)                                                                                \
    enum class EnumName : Underlying { __VA_ARGS__ };                                                                  \
                                                                                                                       \
    struct EnumName##Support : EnumSupportBase< EnumName > {                                                           \
        typedef EnumName enum_type;                                                                                    \
        typedef std::underlying_type_t< enum_type > underlying_type;                                                   \
        EnumName##Support(const std::string tokens) : EnumSupportBase< enum_type >{tokens} {};                         \
        EnumName##Support(const EnumName##Support&) = delete;                                                          \
        EnumName##Support(EnumName##Support&&) noexcept = delete;                                                      \
        EnumName##Support& operator=(const EnumName##Support&) = delete;                                               \
        EnumName##Support& operator=(EnumName##Support&&) noexcept = delete;                                           \
        ~EnumName##Support() = default;                                                                                \
        static EnumName##Support& instance() {                                                                         \
            static EnumName##Support s_instance{#__VA_ARGS__};                                                         \
            return s_instance;                                                                                         \
        };                                                                                                             \
    };                                                                                                                 \
    [[nodiscard]] inline EnumName##Support::enum_type operator|(const EnumName##Support::enum_type a,                  \
                                                                const EnumName##Support::enum_type b) {                \
        return static_cast< EnumName##Support::enum_type >(static_cast< EnumName##Support::underlying_type >(a) |      \
                                                           static_cast< EnumName##Support::underlying_type >(b));      \
    }                                                                                                                  \
    [[nodiscard]] inline EnumName##Support::enum_type operator&(const EnumName##Support::enum_type a,                  \
                                                                const EnumName##Support::enum_type b) {                \
        return static_cast< EnumName##Support::enum_type >(static_cast< EnumName##Support::underlying_type >(a) &      \
                                                           static_cast< EnumName##Support::underlying_type >(b));      \
    }                                                                                                                  \
    [[maybe_unused]] inline EnumName##Support::enum_type operator|=(EnumName##Support::enum_type& a,                   \
                                                                    const EnumName##Support::enum_type b) {            \
        return a = static_cast< EnumName##Support::enum_type >(static_cast< EnumName##Support::underlying_type >(a) |  \
                                                               static_cast< EnumName##Support::underlying_type >(b));  \
    }                                                                                                                  \
    [[maybe_unused]] inline EnumName##Support::enum_type operator&=(EnumName##Support::enum_type& a,                   \
                                                                    const EnumName##Support::enum_type b) {            \
        return a = static_cast< EnumName##Support::enum_type >(static_cast< EnumName##Support::underlying_type >(a) &  \
                                                               static_cast< EnumName##Support::underlying_type >(b));  \
    }                                                                                                                  \
    template < typename charT, typename traits >                                                                       \
    std::basic_ostream< charT, traits >& operator<<(std::basic_ostream< charT, traits >& out_stream,                   \
                                                    const EnumName##Support::enum_type es) {                           \
        std::basic_ostringstream< charT, traits > out_stream_copy{};                                                   \
        out_stream_copy.copyfmt(out_stream);                                                                           \
        out_stream_copy << EnumName##Support::instance().get_name(es);                                                 \
        out_stream << out_stream_copy.str();                                                                           \
        return out_stream;                                                                                             \
    }                                                                                                                  \
    [[nodiscard]] inline const std::string& enum_name(const EnumName##Support::enum_type es) {                         \
        return EnumName##Support::instance().get_name(es);                                                             \
    }

#endif // SISL_ENUM_HPP
