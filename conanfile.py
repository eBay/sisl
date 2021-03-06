#!/usr/bin/env python
# -*- coding: utf-8 -*-
from conans import ConanFile, CMake, tools
import os

class MetricsConan(ConanFile):
    name = "sisl"
    version = "7.0.8"

    license = "Apache"
    url = "https://github.corp.ebay.com/Symbiosis/sisl"
    description = "Library for fast data structures, utilities"
    revision_mode = "scm"

    settings = "arch", "os", "compiler", "build_type"
    options = {
                "shared": ['True', 'False'],
                "fPIC": ['True', 'False'],
                "coverage": ['True', 'False'],
                "sanitize": ['True', 'False'],
                'malloc_impl' : ['libc', 'tcmalloc', 'jemalloc'],
                'prerelease' : ['True', 'False'],
              }
    default_options = (
                        'shared=False',
                        'fPIC=True',
                        'coverage=False',
                        'sanitize=False',
                        'malloc_impl=tcmalloc',
                        'prerelease=True',
                        )

    build_requires = (
                    "benchmark/1.5.0",
                    "gtest/1.10.0",
                )
    requires = (
                    "boost/1.73.0",
                    "spdlog/1.9.2",
                    "evhtp/1.2.18.2",
                    "snappy/1.1.8",
                    "flatbuffers/1.11.0",
                    ("fmt/8.0.1", "override"),
                    "folly/2020.05.04.00",
                    "nlohmann_json/3.8.0",
                    ("openssl/1.1.1g", "override"),
                    "prometheus_cpp/0.7.2",
                    "userspace-rcu/0.11.2",
                    "semver/1.1.0",
                    "jwt-cpp/0.4.0",
                    "cpr/1.5.2",
                    "libcurl/7.70.0",
                    "cxxopts/2.2.1",
                )

    generators = "cmake"
    exports_sources = ("CMakeLists.txt", "cmake/*", "src/*")

    def config_options(self):
        if self.settings.build_type != "Debug":
            del self.options.sanitize
            del self.options.coverage
        elif os.getenv("OVERRIDE_SANITIZE") != None:
            self.options.sanitize = True

    def configure(self):
        if self.settings.build_type == "Debug":
            if self.options.coverage and self.options.sanitize:
                raise ConanInvalidConfiguration("Sanitizer does not work with Code Coverage!")
            if self.options.coverage or self.options.sanitize:
                self.options.malloc_impl = 'libc'

    def requirements(self):
        if self.options.malloc_impl == "jemalloc":
            self.requires("jemalloc/5.2.1")
        elif self.options.malloc_impl == "tcmalloc":
            self.requires("gperftools/2.7.0")

    def build(self):
        cmake = CMake(self)

        definitions = {'CONAN_BUILD_COVERAGE': 'OFF',
                       'CMAKE_EXPORT_COMPILE_COMMANDS': 'ON',
                       'MEMORY_SANITIZER_ON': 'OFF'}
        test_target = None

        run_tests = True
        if self.settings.build_type == "Debug":
            if self.options.sanitize:
                definitions['MEMORY_SANITIZER_ON'] = 'ON'
            elif self.options.coverage:
                definitions['CONAN_BUILD_COVERAGE'] = 'ON'
                test_target = 'coverage'
            else:
                if (None == os.getenv("RUN_TESTS")):
                    run_tests = False

        definitions['MALLOC_IMPL'] = self.options.malloc_impl

        cmake.configure(defs=definitions)
        cmake.build()
        if run_tests:
            #cmake.test(target=test_target, output_on_failure=True)
            cmake.test(target=test_target)

    def package(self):
        self.copy("*.hpp", src="src/", dst="include/sisl", keep_path=True)
        self.copy("*.h", src="src/", dst="include/sisl", keep_path=True)
        self.copy("*.a", dst="lib/", keep_path=False)
        self.copy("*.lib", dst="lib/", keep_path=False)
        self.copy("*.so", dst="lib/", keep_path=False)
        self.copy("*.dll", dst="lib/", keep_path=False)
        self.copy("*.dylib", dst="lib/", keep_path=False)
        self.copy("*.cmake", dst="cmake/", keep_path=False)

    def package_info(self):
        self.cpp_info.libs = tools.collect_libs(self)
        self.cpp_info.cppflags.append("-Wno-unused-local-typedefs")
        self.cpp_info.cppflags.append("-fconcepts")
        self.cpp_info.includedirs = ["include", "include/sisl/"]
        if self.options.prerelease:
            self.cpp_info.cxxflags.append("-D_PRERELEASE=1")
        if self.settings.os == "Linux":
            self.cpp_info.cppflags.append("-D_POSIX_C_SOURCE=200809L")
            self.cpp_info.cppflags.append("-D_FILE_OFFSET_BITS=64")
            self.cpp_info.cppflags.append("-D_LARGEFILE64")
        if self.settings.build_type == "Debug":
            if  self.options.sanitize:
                self.cpp_info.sharedlinkflags.append("-fsanitize=address")
                self.cpp_info.exelinkflags.append("-fsanitize=address")
                self.cpp_info.sharedlinkflags.append("-fsanitize=undefined")
                self.cpp_info.exelinkflags.append("-fsanitize=undefined")
            elif self.options.coverage == 'True':
                self.cpp_info.libs.append('gcov')
        if self.settings.os == "Linux":
            self.cpp_info.libs.extend(["dl"])
            self.cpp_info.exelinkflags.extend(["-export-dynamic"])

        if self.options.malloc_impl == 'jemalloc':
            self.cpp_info.cppflags.append("-DUSE_JEMALLOC=1")
        elif self.options.malloc_impl == 'tcmalloc':
            self.cpp_info.cppflags.append("-DUSING_TCMALLOC=1")
