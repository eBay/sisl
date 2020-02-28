#!/usr/bin/env python
# -*- coding: utf-8 -*-
from conans import ConanFile, CMake, tools

class MetricsConan(ConanFile):
    name = "sisl"
    version = "1.0.2"

    license = "Proprietary"
    url = "https://github.corp.ebay.com/Symbiosis/sisl"
    description = "Sisl library for fast data structures, utilities"
    revision_mode = "scm"

    settings = "arch", "os", "compiler", "build_type"
    options = {
                "shared": ['True', 'False'],
                "fPIC": ['True', 'False'],
                "coverage": ['True', 'False'],
                "sanitize": ['True', 'False'],
              }
    default_options = (
                        'shared=False',
                        'fPIC=True',
                        'coverage=False',
                        'sanitize=False',
                        )

    requires = (("sds_logging/6.1.2@sds/develop"),
                ("benchmark/1.5.0"),
                ("boost/1.72.0"),
                ("gtest/1.10.0"),
                ("evhtp/1.2.18.2"),
                ("folly/2019.09.30.00"),
                ("userspace-rcu/0.10.2"),
                ("prometheus_cpp/0.7.1"),
                ("nlohmann_json/3.7.3"))

    generators = "cmake"
    exports_sources = ("CMakeLists.txt", "cmake/*", "src/*")

    def configure(self):
        if self.options.sanitize:
            self.options.coverage = False

    def build(self):
        cmake = CMake(self)

        definitions = {'CONAN_BUILD_COVERAGE': 'OFF',
                       'MEMORY_SANITIZER_ON': 'OFF'}
        test_target = None

        if self.options.sanitize:
            definitions['MEMORY_SANITIZER_ON'] = 'ON'

        if self.settings.compiler == "gcc" and self.options.coverage == 'True':
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
        if self.options.sanitize:
            self.cpp_info.sharedlinkflags.append("-fsanitize=address")
            self.cpp_info.exelinkflags.append("-fsanitize=address")
            self.cpp_info.sharedlinkflags.append("-fsanitize=undefined")
            self.cpp_info.exelinkflags.append("-fsanitize=undefined")
        elif self.settings.compiler == "gcc" and self.options.coverage == 'True':
            self.cpp_info.libs.append('gcov')
        if self.settings.os == "Linux":
            self.cpp_info.libs.extend(["dl"])
            self.cpp_info.exelinkflags.extend(["-export-dynamic"])
