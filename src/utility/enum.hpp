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
#include <vector>

// #define DECL_ENUM_NAMES(enum_type, enum_values)
//     template <typename T> const char* get_##enum_type##_name(T enum_value) {
//         static char const* const enum_##enum_type##_str[] = {enum_values 0};
//         if (enum_value >= enum_type##_count || enum_value < 0)
//             return "???";
//         else
//             return enum_##enum_type##_str[enum_value];
//     }
//     std::ostream& operator<<(std::ostream& os, enum enum_type rs);

struct EnumSupportBase {
    static const inline std::string UNKNOWN = "???";

    static std::vector< std::string > split(const std::string& s,
                                            const std::regex& delim = std::regex("\\s*(=[^\\s+]\\s*)?,\\s*")) {
        std::vector< std::string > tokens{};
        std::copy(std::regex_token_iterator< std::string::const_iterator >(std::cbegin(s), std::cend(s), delim, -1),
             std::regex_token_iterator< std::string::const_iterator >(), back_inserter(tokens));
        return tokens;
    }

    //    static std::vector<std::string> split(const std::string s, char delim) {
    //        std::stringstream ss(s);
    //        std::string item;
    //        std::vector<std::string> tokens;
    //        while (std::getline(ss, item, delim)) {
    //            auto pos = item.find_first_of ('=');
    //            if (pos != std::string::npos)
    //                item.erase (pos);
    //            boost::trim (item);
    //            tokens.push_back(item);
    //        }
    //        return tokens;
    //    }
};

#define ENUM(EnumName, Underlying, ...)                                                                                \
    enum class EnumName : Underlying { __VA_ARGS__, _count };                                                          \
                                                                                                                       \
    struct EnumName##Support : EnumSupportBase {                                                                       \
        static inline const std::vector< std::string > s_token_names = split(#__VA_ARGS__ /*, ','*/);                  \
        static inline const std::string& get_name(const EnumName enum_value) {                                         \
            const Underlying index{ static_cast < Underlying >(enum_value) };                                          \
            if constexpr (std::is_signed_v< Underlying >) {                                                                \
                if ((index >= static_cast< Underlying >(EnumName::_count)) || (index < 0))                             \
                    return EnumSupportBase::UNKNOWN;                                                                   \
                else                                                                                                   \
                    return s_token_names[index];                                                                       \
            }                                                                                                          \
            else {                                                                                                     \
                if ((index >= static_cast< Underlying >(EnumName::_count)))                                            \
                    return EnumSupportBase::UNKNOWN;                                                                   \
                else                                                                                                   \
                    return s_token_names[index];                                                                       \
            }                                                                                                          \
        }                                                                                                              \
    };                                                                                                                 \
    template < typename charT, typename traits >                                                                       \
    std::basic_ostream< charT, traits >& operator<<(std::basic_ostream< charT, traits >& outStream,                    \
                                                    const EnumName& es) {                                              \
        std::basic_ostringstream< charT, traits > outStringStream;                                                     \
        outStringStream.copyfmt(outStream);                                                                            \
        outStringStream << EnumName##Support::get_name(es);                                                            \
        outStream << outStringStream.str();                                                                            \
        return outStream;                                                                                              \
    }                                                                                                                  \
    static inline const std::string& enum_name(const EnumName& es) { return EnumName##Support::get_name(es); }

//////////////////// VENUM - Value assigned for enum ///////////
struct VEnumSupportBase {
    static const inline std::string UNKNOWN{"???"};

    static std::map< uint64_t, std::string >
    split(std::string s, const std::regex& delim = std::regex("([^,\\s]+)\\s*=\\s*([\\d]+)")) {
        std::map< uint64_t, std::string > tokens;
        std::for_each(std::sregex_iterator(std::cbegin(s), std::cend(s), delim), std::sregex_iterator(),
                      [&](std::smatch const& match) { tokens[std::stoull(match[2])] = match[1]; });
        return tokens;
    }
};

#define VENUM(EnumName, Underlying, ...)                                                                               \
    enum class EnumName : Underlying { __VA_ARGS__ };                                                                  \
                                                                                                                       \
    struct EnumName##Support : VEnumSupportBase {                                                                      \
        static inline const std::map< Underlying, std::string > m_token_names = split(#__VA_ARGS__);                   \
        static inline const std::string& get_name(const EnumName enum_value) {                                         \
            const auto n{ m_token_names.find(static_cast < Underlying >(enum_value)) };                                \
            if (n == std::cend(m_token_names))                                                                         \
                return VEnumSupportBase::UNKNOWN;                                                                      \
            else                                                                                                       \
                return n->second;                                                                                      \
        }                                                                                                              \
    };                                                                                                                 \
                                                                                                                       \
    inline EnumName operator|(EnumName a, EnumName b) {                                                                \
        return static_cast< EnumName >(static_cast< std::underlying_type_t< EnumName > >(a) |                          \
                                       static_cast< std::underlying_type_t< EnumName > >(b));                          \
    }                                                                                                                  \
    inline EnumName operator&(EnumName a, EnumName b) {                                                                \
        return static_cast< EnumName >(static_cast< std::underlying_type_t< EnumName > >(a) &                          \
                                       static_cast< std::underlying_type_t< EnumName > >(b));                          \
    }                                                                                                                  \
    inline EnumName& operator|=(EnumName& a, EnumName b) {                                                             \
        return a = static_cast< EnumName >(static_cast< std::underlying_type_t< EnumName > >(a) |                      \
                                           static_cast< std::underlying_type_t< EnumName > >(b));                      \
    }                                                                                                                  \
    template < typename charT, typename traits >                                                                       \
    std::basic_ostream< charT, traits >& operator<<(std::basic_ostream< charT, traits >& outStream,                    \
                                                    const EnumName& es) {                                              \
        std::basic_ostringstream< charT, traits > outStringStream;                                                     \
        outStringStream.copyfmt(outStream);                                                                            \
        outStringStream << EnumName##Support::get_name(es);                                                            \
        outStream << outStringStream.str();                                                                            \
        return outStream;                                                                                              \
    }                                                                                                                  \
    static inline const std::string& enum_name(const EnumName& es) { return EnumName##Support::get_name(es); }

#endif // SISL_ENUM_HPP
