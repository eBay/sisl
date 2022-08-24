from os.path import join
from conan import ConanFile
from conan.tools.files import copy
from conans.tools import check_min_cppstd
from conans import CMake

required_conan_version = ">=1.50.0"

class SISLConan(ConanFile):
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
                'malloc_impl' : ['libc', 'jemalloc'],
                'with_evhtp' : ['True', 'False'],
              }
    default_options = {
                'shared': False,
                'fPIC': True,
                'malloc_impl': 'libc',
                'with_evhtp': False,
            }

    generators = "cmake", "cmake_find_package"
    exports_sources = ("CMakeLists.txt", "cmake/*", "src/*", "LICENSE")

    def build_requirements(self):
        self.build_requires("benchmark/1.6.1")
        self.build_requires("gtest/1.11.0")


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
        self.requires("semver200/1.1.0")
        self.requires("spdlog/1.10.0")
        self.requires("userspace-rcu/0.11.4")
        self.requires("fmt/8.1.1",          override=True)
        self.requires("libevent/2.1.12",    override=True)
        self.requires("openssl/1.1.1q",     override=True)
        self.requires("xz_utils/5.2.5",     override=True)
        self.requires("zlib/1.2.12",        override=True)
        if self.options.malloc_impl == "jemalloc":
            self.requires("jemalloc/5.2.1")
        if self.options.with_evhtp:
            self.requires("evhtp/1.2.18.2")

    def validate(self):
        if self.info.settings.compiler.cppstd:
            check_min_cppstd(self, 20)

    def configure(self):
        if self.options.shared:
            del self.options.fPIC

    def build(self):
        cmake = CMake(self)

        definitions = {'CMAKE_EXPORT_COMPILE_COMMANDS': 'ON',
                       'MEMORY_SANITIZER_ON': 'OFF',
                       'EVHTP_ON': 'OFF',
                       'MALLOC_IMPL': self.options.malloc_impl}
        test_target = None

        if self.options.with_evhtp:
            definitions['EVHTP_ON'] = 'ON'

        cmake.configure(defs=definitions)
        cmake.build()
        cmake.test(target=test_target)

    def package(self):
        lib_dir = join(self.package_folder, "lib")
        copy(self, "LICENSE", self.source_folder, join(self.package_folder, "licenses/"), keep_path=False)
        copy(self, "*.lib", self.build_folder, lib_dir, keep_path=False)
        copy(self, "*.a", self.build_folder, lib_dir, keep_path=False)
        copy(self, "*.so*", self.build_folder, lib_dir, keep_path=False)
        copy(self, "*.dylib*", self.build_folder, lib_dir, keep_path=False)
        copy(self, "*.dll*", self.build_folder, join(self.package_folder, "bin"), keep_path=False)
        copy(self, "*.so*", self.build_folder, lib_dir, keep_path=False)

        hdr_dir = join(self.package_folder, join("include", "sisl"))

        copy(self, "*.hpp", join(self.source_folder, "src"), hdr_dir, keep_path=True)
        copy(self, "*.h", join(self.source_folder, "src"), hdr_dir, keep_path=True)
        copy(self, "settings_gen.cmake", join(self.source_folder, "cmake"), join(self.package_folder, "cmake"), keep_path=False)

    def package_info(self):
        self.cpp_info.libs = ["sisl"]
        self.cpp_info.cppflags.extend(["-Wno-unused-local-typedefs", "-fconcepts"])

        if self.settings.os == "Linux":
            self.cpp_info.cppflags.append("-D_POSIX_C_SOURCE=200809L")
            self.cpp_info.cppflags.append("-D_FILE_OFFSET_BITS=64")
            self.cpp_info.cppflags.append("-D_LARGEFILE64")
            self.cpp_info.system_libs.append("dl")
            self.cpp_info.exelinkflags.extend(["-export-dynamic"])

        if self.options.malloc_impl == 'jemalloc':
            self.cpp_info.cppflags.append("-DUSE_JEMALLOC=1")
