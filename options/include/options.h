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
#include <cxxopts/cxxopts.hpp>

namespace sds_options {
template<bool...> class bool_pack;
template<bool...b>
using all_true = std::is_same<bool_pack<true, b...>, bool_pack<b..., true>>;

template <typename T>
using shared = std::shared_ptr<T>;

using shared_opt = shared<cxxopts::Options>;

extern shared_opt GetOptions() __attribute__((weak));
struct SdsOption {
   template<class... Args>
   explicit SdsOption(std::string const& group, Args... args)
   {
      auto o = GetOptions();
      o->add_option(group, args...);
   }
};
}

#define SDS_OPTION(r, group, args) \
   sds_options::SdsOption const BOOST_PP_CAT(_option_, BOOST_PP_TUPLE_ELEM(0, args)) \
      {BOOST_PP_STRINGIZE(group), BOOST_PP_TUPLE_REM_CTOR(BOOST_PP_TUPLE_REMOVE(args, 0))};

#define SDS_OPTION_GROUP(group, ...) \
namespace sds_options { \
   struct BOOST_PP_CAT(options_module_, group) { \
      BOOST_PP_SEQ_FOR_EACH(SDS_OPTION, (group), BOOST_PP_TUPLE_TO_SEQ((__VA_ARGS__))); \
   }; \
   extern BOOST_PP_CAT(options_module_, group)* BOOST_PP_CAT(load_options_, group)() { return new BOOST_PP_CAT(options_module_, group)(); } \
}

#define SDS_OPTION_ENABLE(r, _, group) \
   namespace sds_options { \
      struct BOOST_PP_CAT(options_module_, group); \
      BOOST_PP_CAT(options_module_, group)* BOOST_PP_CAT(load_options_, group)(); \
   } \
   static BOOST_PP_CAT(sds_options::options_module_, group) const * BOOST_PP_CAT(options_group_, group);

#define SDS_OPTIONS_ENABLE(...) \
   BOOST_PP_SEQ_FOR_EACH(SDS_OPTION_ENABLE, _, BOOST_PP_TUPLE_TO_SEQ(BOOST_PP_TUPLE_PUSH_FRONT((__VA_ARGS__), main))) \
   static sds_options::shared_opt options_; \
      namespace sds_options { \
      shared_opt GetOptions() {                                         \
          return options_;                                                          \
      }                                                                            \
                                                                                   \
      void SetOptions(shared_opt&& options) { \
          options_ = std::move(options); \
      }                                                                            \
   }

#define SDS_OPTIONS (*sds_options::GetOptions())

#define SDS_OPTION_LOAD(r, _, group) BOOST_PP_CAT(options_group_, group) = BOOST_PP_CAT(sds_options::load_options_, group)();

#define SDS_OPTIONS_LOAD(argc, argv, ...) \
   sds_options::SetOptions(std::make_shared<cxxopts::Options>(argv[0])); \
   BOOST_PP_SEQ_FOR_EACH(SDS_OPTION_LOAD, _, BOOST_PP_TUPLE_TO_SEQ(BOOST_PP_TUPLE_PUSH_FRONT((__VA_ARGS__), main))) \
   SDS_OPTIONS.parse(argc, argv); \
   if (SDS_OPTIONS.count("help")) { \
      std::cout << SDS_OPTIONS.help({}) << std::endl; \
      exit(0); \
   }


