//
// Created by Kadayam, Hari on 12/14/18.
//

#ifndef ASYNC_HTTP_DEMANGLER_HPP
#define ASYNC_HTTP_DEMANGLER_HPP

#include <cxxabi.h>
#include <string>

namespace sisl {
template< typename T >
struct DeMangler {
    static std::string name() {
        int status;
        char *realname = abi::__cxa_demangle(typeid(T).name(), 0, 0, &status);
        std::string str(realname);
        free(realname);

        return str;
    }
};

}

#endif //ASYNC_HTTP_DEMANGLER_HPP
