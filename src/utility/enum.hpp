//
// Created by Finkelstein, Yuri on 1/19/18.
//

#ifndef SISL_ENUM_HPP
#define SISL_ENUM_HPP

#include <string>
#include <regex>
#include <vector>
#include <iostream>
#include <map>

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

    static std::vector< std::string > split(std::string s,
                                            const std::regex& delim = std::regex("\\s*(=[^\\s+]\\s*)?,\\s*")) {
        std::vector< std::string > tokens;
        copy(std::regex_token_iterator< std::string::const_iterator >(s.begin(), s.end(), delim, -1),
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
        static inline const std::vector< std::string > _token_names = split(#__VA_ARGS__ /*, ','*/);                   \
        static inline const std::string& get_name(const EnumName enum_value) {                                         \
            int index = (int)enum_value;                                                                               \
            if (index >= (int)EnumName::_count || index < 0)                                                           \
                return EnumSupportBase::UNKNOWN;                                                                       \
            else                                                                                                       \
                return _token_names[index];                                                                            \
        }                                                                                                              \
    };                                                                                                                 \
    inline std::ostream& operator<<(std::ostream& os, const EnumName& es) {                                            \
        return os << EnumName##Support::get_name(es);                                                                  \
    }                                                                                                                  \
    static inline const std::string& enum_name(const EnumName& es) { return EnumName##Support::get_name(es); }

//////////////////// VENUM - Value assigned for enum ///////////
struct VEnumSupportBase {
    static const inline std::string UNKNOWN = "???";

    static std::map< uint64_t, std::string >
    split(std::string s, const std::regex& delim = std::regex("([^,\\s]+)\\s*=\\s*([\\d]+)")) {
        std::map< uint64_t, std::string > tokens;
        std::for_each(std::sregex_iterator(s.begin(), s.end(), delim), std::sregex_iterator(),
                      [&](std::smatch const& match) { tokens[stoull(match[2])] = match[1]; });
        return tokens;
    }
};

#define VENUM(EnumName, Underlying, ...)                                                                               \
    enum class EnumName : Underlying { __VA_ARGS__ };                                                                  \
                                                                                                                       \
    struct EnumName##Support : VEnumSupportBase {                                                                      \
        static inline const std::map< uint64_t, std::string > _token_names = split(#__VA_ARGS__);                      \
        static inline const std::string& get_name(const EnumName enum_value) {                                         \
            auto n = _token_names.find((uint64_t)enum_value);                                                          \
            if (n == _token_names.end())                                                                               \
                return VEnumSupportBase::UNKNOWN;                                                                      \
            else                                                                                                       \
                return n->second;                                                                                      \
        }                                                                                                              \
    };                                                                                                                 \
                                                                                                                       \
    inline EnumName operator|(EnumName a, EnumName b) {                                                                \
        return static_cast< EnumName >(static_cast< uint64_t >(a) | static_cast< uint64_t >(b));                       \
    }                                                                                                                  \
    inline EnumName operator&(EnumName a, EnumName b) {                                                                \
        return static_cast< EnumName >(static_cast< uint64_t >(a) & static_cast< uint64_t >(b));                       \
    }                                                                                                                  \
    inline EnumName& operator|=(EnumName& a, EnumName b) { return a = a | b; }                                         \
                                                                                                                       \
    inline std::ostream& operator<<(std::ostream& os, const EnumName& es) {                                            \
        return os << EnumName##Support::get_name(es);                                                                  \
    }                                                                                                                  \
    static inline const std::string& enum_name(const EnumName& es) { return EnumName##Support::get_name(es); }

#endif // SISL_ENUM_HPP
