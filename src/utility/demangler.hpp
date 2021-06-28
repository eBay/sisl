//
// Created by Kadayam, Hari on 12/14/18.
//

#ifndef ASYNC_HTTP_DEMANGLER_HPP
#define ASYNC_HTTP_DEMANGLER_HPP

#include <cstdlib>
#include <string>
#include <typeinfo>

#include <cxxabi.h>

namespace sisl {
template< typename T >
struct DeMangler {
    static std::string name() {
        int status;
        const char* const realname{abi::__cxa_demangle(typeid(T).name(), 0, 0, &status)};
        if (realname) {
            const std::string str{realname};
            std::free(realname);
            return str;
        } else
            return std::string{};
    }
};

}

#endif //ASYNC_HTTP_DEMANGLER_HPP
