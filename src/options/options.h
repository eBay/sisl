/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Author/Developer(s): Brian Szymd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 *
 *********************************************************************************/
#pragma once

#include <iostream>
#include <memory>
#include <type_traits>

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/preprocessor/tuple/elem.hpp>
#include <boost/preprocessor/tuple/push_front.hpp>
#include <boost/preprocessor/tuple/rem.hpp>
#include <boost/preprocessor/tuple/remove.hpp>
#include <boost/preprocessor/tuple/to_seq.hpp>
#include <boost/preprocessor/variadic/to_seq.hpp>
#include <boost/preprocessor/variadic/to_tuple.hpp>
#include <cxxopts.hpp>

namespace sisl {
namespace options {
template < bool... >
class bool_pack;
template < bool... b >
using all_true = std::is_same< bool_pack< true, b... >, bool_pack< b..., true > >;

template < typename T >
using shared = std::shared_ptr< T >;

using shared_opt = shared< cxxopts::Options >;
using shared_opt_res = shared< cxxopts::ParseResult >;

extern shared_opt GetOptions() __attribute__((weak));
extern shared_opt_res GetResults() __attribute__((weak));
struct SislOption {
    template < class... Args >
    explicit SislOption(std::string const& group, Args... args) {
        auto o = GetOptions();
        o->add_option(group, args...);
    }
};
} // namespace options
} // namespace sisl

#define SISL_OPTION(r, group, args)                                                                                    \
    sisl::options::SislOption const BOOST_PP_CAT(_option_, BOOST_PP_TUPLE_ELEM(0, args)){                              \
        BOOST_PP_STRINGIZE(group), BOOST_PP_TUPLE_REM_CTOR(BOOST_PP_TUPLE_REMOVE(args, 0))};

#define SISL_OPTION_GROUP(group, ...)                                                                                  \
    namespace sisl {                                                                                                   \
    namespace options {                                                                                                \
    struct BOOST_PP_CAT(options_module_, group) {                                                                      \
        BOOST_PP_SEQ_FOR_EACH(SISL_OPTION, (group), BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))                             \
    };                                                                                                                 \
    extern BOOST_PP_CAT(options_module_, group) * BOOST_PP_CAT(load_options_, group)() {                               \
        return new BOOST_PP_CAT(options_module_, group)();                                                             \
    }                                                                                                                  \
    extern void BOOST_PP_CAT(unload_options_, group)(BOOST_PP_CAT(options_module_, group) * ptr) { delete ptr; }       \
    }                                                                                                                  \
    }

#define SISL_OPTION_ENABLE(r, _, group)                                                                                \
    namespace sisl {                                                                                                   \
    namespace options {                                                                                                \
    struct BOOST_PP_CAT(options_module_, group);                                                                       \
    BOOST_PP_CAT(options_module_, group) * BOOST_PP_CAT(load_options_, group)();                                       \
    void BOOST_PP_CAT(unload_options_, group)(BOOST_PP_CAT(options_module_, group) *);                                 \
    }                                                                                                                  \
    }                                                                                                                  \
    static std::unique_ptr< BOOST_PP_CAT(sisl::options::options_module_, group),                                       \
                            decltype(&BOOST_PP_CAT(sisl::options::unload_options_, group)) >                           \
        BOOST_PP_CAT(options_group_, group)(nullptr, &BOOST_PP_CAT(sisl::options::unload_options_, group));

#define SISL_OPTIONS_ENABLE(...)                                                                                       \
    BOOST_PP_SEQ_FOR_EACH(                                                                                             \
        SISL_OPTION_ENABLE, _,                                                                                         \
        BOOST_PP_TUPLE_TO_SEQ(BOOST_PP_TUPLE_PUSH_FRONT(BOOST_PP_VARIADIC_TO_TUPLE(__VA_ARGS__), main)))               \
    static sisl::options::shared_opt options_;                                                                         \
    static sisl::options::shared_opt_res results_;                                                                     \
    namespace sisl {                                                                                                   \
    namespace options {                                                                                                \
    shared_opt GetOptions() { return options_; }                                                                       \
    shared_opt_res GetResults() { return results_; }                                                                   \
                                                                                                                       \
    void SetOptions(shared_opt&& options) { options_ = std::move(options); }                                           \
    }                                                                                                                  \
    }

#define SISL_OPTIONS (*sisl::options::GetResults())
#define SISL_PARSER (*sisl::options::GetOptions())

#define SISL_OPTION_LOAD(r, _, group)                                                                                  \
    BOOST_PP_CAT(options_group_, group) = decltype(BOOST_PP_CAT(options_group_, group))(                               \
        BOOST_PP_CAT(sisl::options::load_options_, group)(), &BOOST_PP_CAT(sisl::options::unload_options_, group));

#define SISL_OPTIONS_LOAD(argc, argv, ...)                                                                             \
    sisl::options::SetOptions(std::make_shared< cxxopts::Options >(argv[0]));                                          \
    BOOST_PP_SEQ_FOR_EACH(                                                                                             \
        SISL_OPTION_LOAD, _,                                                                                           \
        BOOST_PP_TUPLE_TO_SEQ(BOOST_PP_TUPLE_PUSH_FRONT(BOOST_PP_VARIADIC_TO_TUPLE(__VA_ARGS__), main)))               \
    results_ = std::make_shared< cxxopts::ParseResult >(SISL_PARSER.parse(argc, argv));                                \
    if (SISL_OPTIONS.count("help")) {                                                                                  \
        std::cout << SISL_PARSER.help({}) << std::endl;                                                                \
        exit(0);                                                                                                       \
    }
