#!/usr/bin/env python
# -*- coding: utf-8 -*-
from conans import ConanFile, CMake

class MetricsConan(ConanFile):
    name = "sisl_fds"
    version = "0.1.0"

    license = "Proprietary"
    url = "https://github.corp.ebay.com/Symbiosis/sisl_fds"
    description = "Sisl library for fast data structures, utilities"

    settings = "compiler", "build_type"
    options = {"coverage": ['True', 'False'],
               "sanitize": ['True', 'False']}
    default_options = 'coverage=False', 'sanitize=False'

    requires = (("sds_logging/3.4.1@sds/testing"),
                ("benchmark/1.4.1@oss/stable"),
                ("boost_intrusive/1.66.0@bincrafters/stable"),
                ("boost_filesystem/1.66.0@bincrafters/stable"),
                ("gtest/1.8.1@bincrafters/stable"),
                ("evhtp/1.2.16@oss/stable"),
                ("jsonformoderncpp/3.1.2@vthiery/stable"))

    generators = "cmake"
    exports_sources = "CMakeLists.txt", "cmake/*", "src/*"

    def configure(self):
        if not self.settings.compiler == "gcc":
            del self.options.coverage

    def build(self):
        cmake = CMake(self)

        definitions = {'CONAN_BUILD_COVERAGE': 'OFF',
                       'MEMORY_SANITIZER_ON': 'OFF'}
        test_target = None

        if self.options.coverage == 'True':
            definitions['CONAN_BUILD_COVERAGE'] = 'ON'
            test_target = 'coverage'

        if self.options.sanitize == 'True':
            definitions['MEMORY_SANITIZER_ON'] = 'ON'

        if self.settings.build_type == 'Debug':
            definitions['CMAKE_BUILD_TYPE'] = 'Debug'

        cmake.configure(defs=definitions)
        cmake.build()
        cmake.test(target=test_target)

    def package(self):
        self.copy("*.hpp", src="src/", dst="include", keep_path=False)
        self.copy("*.a", dst="lib/", keep_path=False)
        self.copy("*.lib", dst="lib/", keep_path=False)
        self.copy("*.so", dst="lib/", keep_path=False)
        self.copy("*.dll", dst="lib/", keep_path=False)
        self.copy("*.dylib", dst="lib/", keep_path=False)

    def package_info(self):
        if self.options.coverage == 'True':
            self.cpp_info.libs.append('gcov')
