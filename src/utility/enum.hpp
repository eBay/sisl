//
// Created by Finkelstein, Yuri on 1/19/18.
//

#ifndef SISL_ENUM_HPP
#define SISL_ENUM_HPP

#include <iterator>
#include <map>
#include <ostream>
#include <regex>
#include <sstream>
#include <string>
#include <type_traits>

template < typename EnumType >
struct EnumSupportBase {
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
        while (current_pos < tokens_string.size()) {
            const size_t delim{tokens_string.find_first_of(",=", current_pos)};
            const std::string token{trim(tokens_string.substr(current_pos, delim - current_pos))};
            if ((delim != std::string::npos) && (tokens_string[delim] == '=')) {
                const size_t comma_pos{tokens_string.find(',', delim + 1)};
                const size_t length{comma_pos != std::string::npos ? comma_pos - delim : comma_pos};
                if constexpr (std::is_unsigned_v< underlying_type >) {
                    last_value =
                        static_cast< underlying_type >(std::stoull(trim(tokens_string.substr(delim + 1, length))));
                } else {
                    last_value =
                        static_cast< underlying_type >(std::stoll(trim(tokens_string.substr(delim + 1, length))));
                }
                current_pos = ((comma_pos != std::string::npos) ? comma_pos + 1 : comma_pos);
            } else {
                if (!m_tokens.empty()) ++last_value;
                current_pos = ((delim != std::string::npos) ? delim + 1 : delim);
            }
            m_tokens[last_value] = token;
        }
    }
    EnumSupportBase(const EnumSupportBase&) = delete;
    EnumSupportBase(EnumSupportBase&&) noexcept = delete;
    EnumSupportBase& operator=(const EnumSupportBase&) = delete;
    EnumSupportBase& operator=(EnumSupportBase&&) noexcept = delete;
    ~EnumSupportBase() = default;

    const std::string& get_name(const enum_type enum_value) const {
        static const std::string unknown{"???"};
        const underlying_type val{static_cast< underlying_type >(enum_value)};
        const auto itr{m_tokens.find(val)};
        if (itr == std::cend(m_tokens))
            return unknown;
        else
            return itr->second;
    }

    std::map< underlying_type, std::string > m_tokens;
};

#define ENUM(EnumName, Underlying, ...)                                                                                \
    enum class EnumName : Underlying { __VA_ARGS__ };                                                                  \
                                                                                                                       \
    struct EnumName##Support : EnumSupportBase<EnumName> {                                                             \
        typedef EnumName enum_type;                                                                                    \
        typedef std::underlying_type_t< enum_type > underlying_type;                                                   \
        EnumName##Support(const std::string tokens) :                                                                  \
            EnumSupportBase< enum_type >{tokens} {};                                                                   \
        EnumName##Support(const EnumName##Support&) = delete;                                                          \
        EnumName##Support(EnumName##Support&&) noexcept = delete;                                                      \
        EnumName##Support& operator=(const EnumName##Support&) = delete;                                               \
        EnumName##Support& operator=(EnumName##Support&&) noexcept = delete;                                           \
        static EnumName##Support& instance() {                                                                         \
            static EnumName##Support s_instance{#__VA_ARGS__};                                                         \
            return s_instance;                                                                                         \
        };                                                                                                             \
    };                                                                                                                 \
    inline EnumName##Support::enum_type operator|(const EnumName##Support::enum_type a,                                \
                                                  const EnumName##Support::enum_type b) {                              \
        return static_cast< EnumName##Support::enum_type >(static_cast< EnumName##Support::underlying_type >(a) |      \
                                                           static_cast< EnumName##Support::underlying_type >(b));      \
    }                                                                                                                  \
    inline EnumName##Support::enum_type operator&(const EnumName##Support::enum_type a,                                \
                                                  const EnumName##Support::enum_type b) {                              \
        return static_cast< EnumName##Support::enum_type >(static_cast< EnumName##Support::underlying_type >(a) &      \
                                                           static_cast< EnumName##Support::underlying_type >(b));      \
    }                                                                                                                  \
    inline EnumName##Support::enum_type operator|=(EnumName##Support::enum_type& a,                                    \
                                                   const EnumName##Support::enum_type b) {                             \
        return a = static_cast< EnumName##Support::enum_type >(static_cast< EnumName##Support::underlying_type >(a) |  \
                                                               static_cast< EnumName##Support::underlying_type >(b));  \
    }                                                                                                                  \
    inline EnumName##Support::enum_type operator&=(EnumName##Support::enum_type& a,                                    \
                                                   const EnumName##Support::enum_type b) {                             \
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
    static inline const std::string& enum_name(const EnumName##Support::enum_type es) {                                                   \
        return EnumName##Support::instance().get_name(es);                                                             \
    }

//////////////////// VENUM - Value assigned for enum ///////////
#define VENUM ENUM

#endif // SISL_ENUM_HPP
