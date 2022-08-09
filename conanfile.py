from conans import ConanFile, CMake, tools
import os

class MetricsConan(ConanFile):
    name = "sisl"
    version = "8.0.1"
    homepage = "https://github.com/eBay/sisl"
    description = "Library for fast data structures, utilities"
    topics = ("ebay", "components", "core", "efficiency")
    url = "https://github.com/eBay/sisl"
    license = "Apache-2.0"

    settings = "arch", "os", "compiler", "build_type"
    options = {
                "shared": ['True', 'False'],
                "fPIC": ['True', 'False'],
                "coverage": ['True', 'False'],
                "sanitize": ['True', 'False'],
                'malloc_impl' : ['libc', 'tcmalloc', 'jemalloc'],
                'prerelease' : ['True', 'False'],
                'with_evhtp' : ['True', 'False'],
              }
    default_options = {
                'shared': False,
                'fPIC': True,
                'coverage': False,
                'sanitize': False,
                'malloc_impl': 'libc',
                'prerelease': True,
                'with_evhtp': True,
            }

    build_requires = (
                    # Generic packages (conan-center)
                    "benchmark/1.6.1",
                    "gtest/1.11.0",
                )

    generators = "cmake", "cmake_find_package"
    exports_sources = ("CMakeLists.txt", "cmake/*", "src/*", "LICENSE")

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
        if self.options.shared:
            del self.options.fPIC

    def requirements(self):
            # Custom packages
        self.requires("prometheus-cpp/1.0.0")

        # Generic packages (conan-center)
        self.requires("boost/1.79.0")
        self.requires("cpr/1.8.1")
        self.requires("cxxopts/2.2.1")
        self.requires("flatbuffers/1.12.0")
        self.requires("folly/2022.01.31.00")
        self.requires("jwt-cpp/0.4.0")
        self.requires("nlohmann_json/3.10.5")
        self.requires("semver.c/1.0.0")
        self.requires("spdlog/1.10.0")
        self.requires("userspace-rcu/0.11.4")
        self.requires("fmt/8.1.1",          override=True)
        self.requires("libevent/2.1.12",    override=True)
        self.requires("openssl/1.1.1q",     override=True)
        self.requires("xz_utils/5.2.5",     override=True)
        self.requires("zlib/1.2.12",        override=True)
        if self.options.malloc_impl == "jemalloc":
            self.requires("jemalloc/5.2.1")
        elif self.options.malloc_impl == "tcmalloc":
            self.requires("gperftools/2.7.0")
        if self.options.with_evhtp:
            self.requires("evhtp/1.2.18.2")

    def build(self):
        cmake = CMake(self)

        definitions = {'CONAN_BUILD_COVERAGE': 'OFF',
                       'CMAKE_EXPORT_COMPILE_COMMANDS': 'ON',
                       'MEMORY_SANITIZER_ON': 'OFF',
                       'EVHTP_ON': 'OFF'}
        test_target = None

        if self.options.with_evhtp:
            definitions['EVHTP_ON'] = 'ON'

        if self.settings.build_type == "Debug":
            if self.options.sanitize:
                definitions['MEMORY_SANITIZER_ON'] = 'ON'
            elif self.options.coverage:
                definitions['CONAN_BUILD_COVERAGE'] = 'ON'
                test_target = 'coverage'

        definitions['MALLOC_IMPL'] = self.options.malloc_impl

        cmake.configure(defs=definitions)
        cmake.build()
        cmake.test(target=test_target)

    def package(self):
        self.copy(pattern="LICENSE*", dst="licenses")
        self.copy("*.hpp", src="src/", dst="include/sisl", keep_path=True)
        self.copy("*.h", src="src/", dst="include/sisl", keep_path=True)
        self.copy("*.a", dst="lib/", keep_path=False)
        self.copy("*.lib", dst="lib/", keep_path=False)
        self.copy("*.so", dst="lib/", keep_path=False)
        self.copy("*.dll", dst="lib/", keep_path=False)
        self.copy("*.dylib", dst="lib/", keep_path=False)
        self.copy("*.cmake", dst="cmake/", keep_path=False)

    def package_info(self):
        self.cpp_info.libs = ["sisl"]
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
                self.cpp_info.system_libs.append('gcov')
        if self.settings.os == "Linux":
            self.cpp_info.system_libs.append("dl")
            self.cpp_info.exelinkflags.extend(["-export-dynamic"])

        if self.options.malloc_impl == 'jemalloc':
            self.cpp_info.cppflags.append("-DUSE_JEMALLOC=1")
        elif self.options.malloc_impl == 'tcmalloc':
            self.cpp_info.cppflags.append("-DUSING_TCMALLOC=1")
