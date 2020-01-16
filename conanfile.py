#!/usr/bin/env python
# -*- coding: utf-8 -*-
from conans import ConanFile, CMake, tools

class MetricsConan(ConanFile):
    name = "sisl"
    version = "0.3.15"

    license = "Proprietary"
    url = "https://github.corp.ebay.com/Symbiosis/sisl"
    description = "Sisl library for fast data structures, utilities"
    revision_mode = "scm"

    settings = "arch", "os", "compiler", "build_type", "sanitize"
    options = {"shared": ['True', 'False'],
               "fPIC": ['True', 'False'],
               "coverage": ['True', 'False']}
    default_options = ('shared=False', 'fPIC=True', 'coverage=False')

    requires = (("sds_logging/6.1.0@sds/develop"),
                ("benchmark/1.5.0@oss/stable"),
                ("boost_intrusive/1.69.0@bincrafters/stable"),
                ("boost_dynamic_bitset/1.69.0@bincrafters/stable"),
                ("boost_filesystem/1.69.0@bincrafters/stable"),
                ("boost_preprocessor/1.69.0@bincrafters/stable"),
                ("gtest/1.8.1@bincrafters/stable"),
                ("evhtp/1.2.18.1@oss/stable"),
                ("folly/2019.09.23.00@bincrafters/stable"),
                ("userspace-rcu/0.10.1@oss/stable"),
                ("OpenSSL/1.1.1c@conan/stable"),
                ("sds_prometheus/0.7.1@sds/stable"),
                ("jsonformoderncpp/3.7.0@vthiery/stable"),
                ("zstd/1.4.0@bincrafters/stable"))

    generators = "cmake"
    exports_sources = ("CMakeLists.txt", "cmake/*", "src/*")

    def configure(self):
        if self.settings.compiler != "gcc":
            del self.options.coverage
        #if self.settings.sanitize != None:
        #    del self.options.coverage

    def build(self):
        cmake = CMake(self)

        definitions = {'CONAN_BUILD_COVERAGE': 'OFF',
                       'MEMORY_SANITIZER_ON': 'OFF'}
        test_target = None

        if self.settings.sanitize != None:
            definitions['MEMORY_SANITIZER_ON'] = 'ON'
        elif self.settings.compiler == "gcc" and self.options.coverage == 'True':
            definitions['CONAN_BUILD_COVERAGE'] = 'ON'
            test_target = 'coverage'

        if self.settings.build_type == 'Debug':
            definitions['CMAKE_BUILD_TYPE'] = 'Debug'

        cmake.configure(defs=definitions)
        cmake.build()
        #cmake.test(target=test_target, output_on_failure=True)
        cmake.test(target=test_target)

    def package(self):
        self.copy("*.hpp", src="src/", dst="include/", keep_path=True)
        self.copy("*.h", src="src/", dst="include/", keep_path=True)
        self.copy("*.a", dst="lib/", keep_path=False)
        self.copy("*.lib", dst="lib/", keep_path=False)
        self.copy("*.so", dst="lib/", keep_path=False)
        self.copy("*.dll", dst="lib/", keep_path=False)
        self.copy("*.dylib", dst="lib/", keep_path=False)

    def package_info(self):
        self.cpp_info.libs = tools.collect_libs(self)
        self.cpp_info.cppflags.append("-Wno-unused-local-typedefs")
        if self.settings.sanitize != None:
            self.cpp_info.sharedlinkflags.append("-fsanitize=address")
            self.cpp_info.exelinkflags.append("-fsanitize=address")
            self.cpp_info.sharedlinkflags.append("-fsanitize=undefined")
            self.cpp_info.exelinkflags.append("-fsanitize=undefined")
        elif self.settings.compiler == "gcc" and self.options.coverage == 'True':
            self.cpp_info.libs.append('gcov')
        if self.settings.os == "Linux":
            self.cpp_info.libs.extend(["dl"])
            self.cpp_info.exelinkflags.extend(["-export-dynamic"])
