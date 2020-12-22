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

struct EnumSupportBase {
    template < typename EnumName >
    static std::map< std::underlying_type_t< EnumName >, std::string >
    split(std::string s, const std::regex& delim = std::regex("([^,\\s]+)\\s*=\\s*([\\d]+)")) {
        std::map< std::underlying_type_t<EnumName>, std::string > tokens;
        std::underlying_type_t< EnumName > last_value{};
        std::for_each(std::sregex_iterator(std::cbegin(s), std::cend(s), delim), std::sregex_iterator(),
                      [&](std::smatch const& match) {
                          if constexpr (std::is_unsigned_v< std::underlying_type_t< EnumName > >) {
                              if (match.size() == 3) {
                                  last_value = static_cast< std::underlying_type_t< EnumName > >(std::stoull(match[2]));
                                  tokens[last_value] = match[1];
                              } else
                              {
                                  tokens[++last_value] = match[1];
                              }
                          } else {
                              if (match.size() == 3) {
                                  last_value = static_cast< std::underlying_type_t< EnumName > >(std::stoll(match[2]));
                                  tokens[last_value] = match[1];
                              } else {
                                  tokens[++last_value] = match[1];
                              }
                          }
                      });
        return tokens;
    }
};

#define ENUM(EnumName, Underlying, ...)                                                                                \
    enum class EnumName : Underlying { __VA_ARGS__ };                                                                  \
                                                                                                                       \
    struct EnumName##Support : EnumSupportBase {                                                                       \
        const std::map< Underlying, std::string > m_token_names{split< EnumName >(#__VA_ARGS__)};                      \
        const std::string& get_name(const EnumName enum_value) {                                                       \
            static const std::string unknown { "???" };                                                                \
            const auto n{m_token_names.find(static_cast< Underlying >(enum_value))};                                   \
            if (n == std::cend(m_token_names))                                                                         \
                return unknown;                                                                                        \
            else                                                                                                       \
                return n->second;                                                                                      \
        }                                                                                                              \
        EnumName##Support() = default;                                                                                 \
        EnumName##Support(const EnumName##Support&) = delete;                                                          \
        EnumName##Support(EnumName##Support&&) noexcept = delete;                                                      \
        EnumName##Support& operator=(const EnumName##Support&) = delete;                                               \
        EnumName##Support& operator=(EnumName##Support&&) noexcept = delete;                                           \
        static EnumName##Support& instance() {                                                                         \
            static EnumName##Support s_instance{};                                                                     \
            return s_instance;                                                                                         \
        };                                                                                                             \
    };                                                                                                                 \
    inline EnumName operator|(EnumName a, EnumName b) {                                                                \
        return static_cast< EnumName >(static_cast< std::underlying_type_t< EnumName > >(a) |                          \
                                       static_cast< std::underlying_type_t< EnumName > >(b));                          \
    }                                                                                                                  \
    inline EnumName operator&(EnumName a, EnumName b) {                                                                \
        return static_cast< EnumName >(static_cast< std::underlying_type_t< EnumName > >(a) &                          \
                                       static_cast< std::underlying_type_t< EnumName > >(b));                          \
    }                                                                                                                  \
    inline EnumName operator|=(EnumName& a, EnumName b) {                                                              \
        return a = static_cast< EnumName >(static_cast< std::underlying_type_t< EnumName > >(a) |                      \
                                           static_cast< std::underlying_type_t< EnumName > >(b));                      \
    }                                                                                                                  \
    template < typename charT, typename traits >                                                                       \
    std::basic_ostream< charT, traits >& operator<<(std::basic_ostream< charT, traits >& out_stream,                   \
                                                    const EnumName es) {                                               \
        std::basic_ostringstream< charT, traits > out_stream_copy{};                                                   \
        out_stream_copy.copyfmt(out_stream);                                                                           \
        out_stream_copy << EnumName##Support::instance().get_name(es);                                                 \
        out_stream << out_stream_copy.str();                                                                           \
        return out_stream;                                                                                             \
    }                                                                                                                  \
    static inline const std::string& enum_name(const EnumName es) { return EnumName##Support::instance().get_name(es); }

//////////////////// VENUM - Value assigned for enum ///////////
#define VENUM ENUM

#endif // SISL_ENUM_HPP
