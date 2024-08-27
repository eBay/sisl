from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMakeToolchain, CMakeDeps, CMake, cmake_layout
from conan.tools.files import copy
from os.path import join

required_conan_version = ">=1.60.0"

class SISLConan(ConanFile):
    name = "sisl"
    version = "8.8.0"
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
                'prerelease' : ['True', 'False'],
                'malloc_impl' : ['libc', 'tcmalloc', 'jemalloc'],
              }
    default_options = {
                'shared': False,
                'fPIC': True,
                'coverage': False,
                'sanitize': False,
                'prerelease': True,
                'malloc_impl': 'tcmalloc',
            }

    exports_sources = ("CMakeLists.txt", "cmake/*", "include/*", "src/*", "LICENSE")

    def build_requirements(self):
        self.test_requires("gtest/1.14.0")
        self.test_requires("benchmark/1.8.2")
        self.test_requires("pistache/0.0.5")

    def requirements(self):
        # Generic packages (conan-center)
        self.requires("boost/1.79.0")
        if self.settings.os in ["Linux"]:
            self.requires("breakpad/cci.20230127")
        self.requires("cpr/1.8.1")
        self.requires("cxxopts/2.2.1")
        self.requires("flatbuffers/1.12.0")
        if self.settings.os in ["Linux"]:
            self.requires("folly/2022.01.31.00")
        self.requires("grpc/[>=1.50]")
        self.requires("jwt-cpp/0.4.0")
        self.requires("nlohmann_json/3.11.2")
        self.requires("prometheus-cpp/1.0.1")
        self.requires("spdlog/1.11.0")
        if self.settings.os in ["Linux"]:
            self.requires("userspace-rcu/0.11.4")
        self.requires("zmarok-semver/1.1.0")
        self.requires("fmt/8.1.1",          override=True)
        self.requires("libevent/2.1.12",    override=True)
        self.requires("openssl/1.1.1s",     override=True)
        self.requires("libcurl/8.4.0",      override=True)
        self.requires("xz_utils/5.2.5",     override=True)
        self.requires("zlib/1.2.12",        override=True)
        self.requires("lz4/1.9.4",          override=True)
        self.requires("zstd/1.5.5",         override=True)
        if self.options.malloc_impl == "jemalloc":
            self.requires("jemalloc/5.2.1")
        elif self.options.malloc_impl == "tcmalloc":
            self.requires("gperftools/2.7.0")

    def validate(self):
        if self.info.settings.compiler.cppstd:
            check_min_cppstd(self, 17)

    def configure(self):
        if self.settings.compiler in ["gcc"]:
            self.options['pistache'].with_ssl: True
        if self.options.shared:
            del self.options.fPIC
        if self.settings.build_type == "Debug":
            if self.options.coverage and self.options.sanitize:
                raise ConanInvalidConfiguration("Sanitizer does not work with Code Coverage!")
            if self.options.coverage or self.options.sanitize:
                self.options.malloc_impl = 'libc'

    def layout(self):
        cmake_layout(self)

    def generate(self):
        # This generates "conan_toolchain.cmake" in self.generators_folder
        tc = CMakeToolchain(self)
        tc.variables["CONAN_CMAKE_SILENT_OUTPUT"] = "ON"
        tc.variables["CTEST_OUTPUT_ON_FAILURE"] = "ON"
        tc.variables["MEMORY_SANITIZER_ON"] = "OFF"
        tc.variables["BUILD_COVERAGE"] = "OFF"
        tc.variables['MALLOC_IMPL'] = self.options.malloc_impl
        tc.preprocessor_definitions["PACKAGE_VERSION"] = self.version
        tc.preprocessor_definitions["PACKAGE_NAME"] = self.name
        if self.options.get_safe("prerelease") or (self.settings.build_type == "Debug"):
            tc.preprocessor_definitions["_PRERELEASE"] = "1"
            tc.variables["_PRERELEASE"] = "ON"
        if self.settings.build_type == "Debug":
            tc.preprocessor_definitions["_PRERELEASE"] = "1"
            if self.options.get_safe("coverage"):
                tc.variables['BUILD_COVERAGE'] = 'ON'
            elif self.options.get_safe("sanitize"):
                tc.variables['MEMORY_SANITIZER_ON'] = 'ON'
        tc.generate()

        # This generates "boost-config.cmake" and "grpc-config.cmake" etc in self.generators_folder
        deps = CMakeDeps(self)
        deps.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
        if not self.conf.get("tools.build:skip_test", default=False):
            cmake.test()

    def package(self):
        lib_dir = join(self.package_folder, "lib")
        copy(self, "LICENSE", self.source_folder, join(self.package_folder, "licenses"), keep_path=False)
        copy(self, "*.lib", self.build_folder, lib_dir, keep_path=False)
        copy(self, "*.a", self.build_folder, lib_dir, keep_path=False)
        copy(self, "*.so*", self.build_folder, lib_dir, keep_path=False)
        copy(self, "*.dylib*", self.build_folder, lib_dir, keep_path=False)
        copy(self, "*.dll*", self.build_folder, join(self.package_folder, "bin"), keep_path=False)
        copy(self, "*.so*", self.build_folder, lib_dir, keep_path=False)
        copy(self, "*.proto", join(self.source_folder, "src", "flip", "proto"), join(self.package_folder, "proto", "flip"), keep_path=False)
        copy(self, "*", join(self.source_folder, "src", "flip", "client", "python"), join(self.package_folder, "bindings", "flip", "python"), keep_path=False)
        copy(self, "*.py", join(self.build_folder, "src", "flip", "proto"), join(self.package_folder, "bindings", "flip", "python"), keep_path=False)

        copy(self, "*.h*", join(self.source_folder, "include"), join(self.package_folder, "include"), keep_path=True)

        gen_dir = join(self.package_folder, "include", "sisl")
        copy(self, "*.pb.h", join(self.build_folder, "src"), gen_dir, keep_path=True)
        copy(self, "*security_config_generated.h", join(self.build_folder, "src"), gen_dir, keep_path=True)
        copy(self, "settings_gen.cmake", join(self.source_folder, "cmake"), join(self.package_folder, "cmake"), keep_path=False)

    def package_info(self):
        self.cpp_info.libs = ["sisl"]

        if self.settings.compiler == "gcc":
            self.cpp_info.cppflags.extend(["-fconcepts"])

        if self.options.get_safe("prerelease") or (self.settings.build_type == "Debug"):
            self.cpp_info.defines.append("_PRERELEASE=1")

        if self.settings.os == "Linux":
            self.cpp_info.libs.append("flip")
            self.cpp_info.cppflags.append("-D_POSIX_C_SOURCE=200809L")
            self.cpp_info.cppflags.append("-D_FILE_OFFSET_BITS=64")
            self.cpp_info.cppflags.append("-D_LARGEFILE64")
            self.cpp_info.system_libs.extend(["dl", "pthread"])
            self.cpp_info.exelinkflags.extend(["-export-dynamic"])

        if  self.options.sanitize:
            self.cpp_info.sharedlinkflags.append("-fsanitize=address")
            self.cpp_info.exelinkflags.append("-fsanitize=address")
            self.cpp_info.sharedlinkflags.append("-fsanitize=undefined")
            self.cpp_info.exelinkflags.append("-fsanitize=undefined")
        if self.options.malloc_impl == 'jemalloc':
            self.cpp_info.cppflags.append("-DUSE_JEMALLOC=1")
        elif self.options.malloc_impl == 'tcmalloc':
            self.cpp_info.cppflags.append("-DUSING_TCMALLOC=1")
            self.cpp_info.libdirs += self.deps_cpp_info["gperftools"].lib_paths
            self.cpp_info.libs += ["tcmalloc"]
