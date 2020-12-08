//
// Copyright 2018, eBay Corporation
//

#pragma once

#include <iostream>
#include <memory>

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
#include <cxxopts/cxxopts.hpp>


namespace sds_options {
template <typename... Args>
bool all_true(Args... args) { return (... && args); }

typedef std::shared_ptr<cxxopts::Options> shared_opt;
typedef std::shared_ptr<cxxopts::ParseResult> shared_opt_res;

extern shared_opt GetOptions() __attribute__((weak));
extern shared_opt_res GetResults() __attribute__((weak));
struct SdsOption {
  template <class... Args>
  explicit SdsOption(std::string const& group, Args... args) {
    auto o = GetOptions();
    o->add_option(group, args...);
  }
};
}  // namespace sds_options

#define SDS_OPTION(r, group, args)                                         \
  sds_options::SdsOption const BOOST_PP_CAT(_option_,                      \
                                            BOOST_PP_TUPLE_ELEM(0, args)){ \
      BOOST_PP_STRINGIZE(group),                                           \
      BOOST_PP_TUPLE_REM_CTOR(BOOST_PP_TUPLE_REMOVE(args, 0))};

#define SDS_OPTION_GROUP(group, ...)                                     \
  namespace sds_options {                                                \
  struct BOOST_PP_CAT(options_module_, group) {                          \
    BOOST_PP_SEQ_FOR_EACH(SDS_OPTION, (group),                           \
                          BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))         \
  };                                                                     \
  extern BOOST_PP_CAT(options_module_, group) *                          \
      BOOST_PP_CAT(load_options_, group)() {                             \
    return new BOOST_PP_CAT(options_module_, group)();                   \
  }                                                                      \
  extern void BOOST_PP_CAT(unload_options_,                              \
                           group)(BOOST_PP_CAT(options_module_, group) * \
                                  ptr) {                                 \
    delete ptr;                                                          \
  }                                                                      \
  }

#define SDS_OPTION_ENABLE(r, _, group)                                         \
  namespace sds_options {                                                      \
  struct BOOST_PP_CAT(options_module_, group);                                 \
  BOOST_PP_CAT(options_module_, group) * BOOST_PP_CAT(load_options_, group)(); \
  void BOOST_PP_CAT(unload_options_,                                           \
                    group)(BOOST_PP_CAT(options_module_, group) *);            \
  }                                                                            \
  static std::unique_ptr<BOOST_PP_CAT(sds_options::options_module_, group),    \
                         decltype(&BOOST_PP_CAT(sds_options::unload_options_,  \
                                                group))>                       \
      BOOST_PP_CAT(options_group_, group)(                                     \
          nullptr, &BOOST_PP_CAT(sds_options::unload_options_, group));

#define SDS_OPTIONS_ENABLE(...)                                              \
  BOOST_PP_SEQ_FOR_EACH(SDS_OPTION_ENABLE, _,                                \
                        BOOST_PP_TUPLE_TO_SEQ(BOOST_PP_TUPLE_PUSH_FRONT(     \
                            BOOST_PP_VARIADIC_TO_TUPLE(__VA_ARGS__), main))) \
  static sds_options::shared_opt options_;                                   \
  static sds_options::shared_opt_res results_;                               \
  namespace sds_options {                                                    \
  shared_opt GetOptions() { return options_; }                               \
  shared_opt_res GetResults() { return results_; }                           \
                                                                             \
  void SetOptions(shared_opt&& options) { options_ = std::move(options); }   \
  }

#define SDS_OPTIONS (*sds_options::GetResults())
#define SDS_PARSER (*sds_options::GetOptions())

#define SDS_OPTION_LOAD(r, _, group)                         \
  BOOST_PP_CAT(options_group_, group) =                      \
      decltype(BOOST_PP_CAT(options_group_, group))(         \
          BOOST_PP_CAT(sds_options::load_options_, group)(), \
          &BOOST_PP_CAT(sds_options::unload_options_, group));

#define SDS_OPTIONS_LOAD(argc, argv, ...)                                    \
  sds_options::SetOptions(std::make_shared<cxxopts::Options>(argv[0]));      \
  BOOST_PP_SEQ_FOR_EACH(SDS_OPTION_LOAD, _,                                  \
                        BOOST_PP_TUPLE_TO_SEQ(BOOST_PP_TUPLE_PUSH_FRONT(     \
                            BOOST_PP_VARIADIC_TO_TUPLE(__VA_ARGS__), main))) \
  results_ =                                                                 \
      std::make_shared<cxxopts::ParseResult>(SDS_PARSER.parse(argc, argv));  \
  if (SDS_OPTIONS.count("help")) {                                           \
    std::cout << SDS_PARSER.help({}) << std::endl;                           \
    exit(0);                                                                 \
  }

