from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMakeToolchain, CMakeDeps, CMake, cmake_layout
from conan.tools.files import copy
from os.path import join

required_conan_version = ">=1.60.0"

class SISLConan(ConanFile):
    name = "sisl"
    version = "12.2.4"

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
                'metrics': ['False', 'True'],
                'grpc': ['False', 'True'],
                'malloc_impl' : ['libc', 'tcmalloc', 'jemalloc'],
              }
    default_options = {
                'shared': False,
                'fPIC': True,
                'coverage': False,
                'sanitize': False,
                'prerelease': False,
                'metrics': True,
                'grpc': True,
                'malloc_impl': 'libc',
            }

    exports_sources = (
                "LICENSE",
                "CMakeLists.txt",
                "cmake/*",
                "include/*",
                "src/*",
            )

    def _min_cppstd(self):
        return 20

    def validate(self):
        if self.settings.compiler.get_safe("cppstd"):
            check_min_cppstd(self, self._min_cppstd())

    def configure(self):
        if not self.options.metrics and self.options.grpc:
            raise ConanInvalidConfiguration("gRPC support requires metrics option!")

        if self.settings.compiler in ["gcc"]:
            self.options['pistache'].with_ssl: True
        if self.options.shared:
            self.options.rm_safe("fPIC")
        if self.settings.build_type == "Debug":
            self.options.rm_safe("prerelease")
            if self.options.coverage and self.options.sanitize:
                raise ConanInvalidConfiguration("Sanitizer does not work with Code Coverage!")
            if self.conf.get("tools.build:skip_test", default=False):
                if self.options.coverage or self.options.sanitize:
                    raise ConanInvalidConfiguration("Coverage/Sanitizer requires Testing!")

    def build_requirements(self):
        self.test_requires("gtest/1.14.0")
        if self.options.metrics:
            self.test_requires("benchmark/1.8.2")

    def requirements(self):
        # Required
        self.requires("boost/1.83.0", transitive_headers=True)
        self.requires("cxxopts/3.1.1", transitive_headers=True)
        self.requires("nlohmann_json/3.11.2", transitive_headers=True)
        self.requires("spdlog/1.12.0", transitive_headers=True)
        self.requires("zmarok-semver/1.1.0", transitive_headers=True)
        if self.settings.os in ["Linux"]:
            self.requires("breakpad/cci.20210521")
        self.requires("fmt/10.0.0",  override=True)

        if self.options.metrics:
            self.requires("flatbuffers/23.5.26", transitive_headers=True)
            self.requires("folly/nu2.2023.12.18.00", transitive_headers=True)
            self.requires("prometheus-cpp/1.1.0", transitive_headers=True)
            self.requires("userspace-rcu/nu2.0.14.0", transitive_headers=True)
            self.requires("libcurl/8.4.0",  override=True)
            self.requires("xz_utils/5.4.5",  override=True)

        if self.options.grpc:
            self.requires("grpc/1.54.3", transitive_headers=True)

        # Memory allocation
        if self.options.malloc_impl == "tcmalloc":
            self.requires("gperftools/2.15", transitive_headers=True)
        elif self.options.malloc_impl == "jemalloc":
            self.requires("jemalloc/5.3.0", transitive_headers=True)

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
        self.cpp_info.components["options"].libs = ["sisl_options"]
        self.cpp_info.components["options"].set_property("pkg_config_name", f"libsisl_options")
        self.cpp_info.components["options"].requires.extend([
                "boost::boost",
                "cxxopts::cxxopts",
                ])

        self.cpp_info.components["logging"].libs = ["sisl_logging"]
        self.cpp_info.components["logging"].set_property("pkg_config_name", f"libsisl_logging")
        self.cpp_info.components["logging"].requires.extend([
                "options",
                "boost::boost",
                "breakpad::breakpad",
                "nlohmann_json::nlohmann_json",
                "spdlog::spdlog",
                ])
        self.cpp_info.components["logging"].sharedlinkflags.append("-rdynamic")
        self.cpp_info.components["logging"].exelinkflags.append("-rdynamic")

        self.cpp_info.components["sobject"].libs = ["sisl_sobject"]
        self.cpp_info.components["sobject"].set_property("pkg_config_name", f"libsisl_sobject")
        self.cpp_info.components["sobject"].requires.extend([
                "logging",
                "nlohmann_json::nlohmann_json",
                ])
        self.cpp_info.components["file_watcher"].libs = ["sisl_file_watcher"]
        self.cpp_info.components["file_watcher"].set_property("pkg_config_name", f"libsisl_file_watcher")
        self.cpp_info.components["file_watcher"].requires.extend([
                "logging",
                ])
        self.cpp_info.components["version"].libs = ["sisl_version"]
        self.cpp_info.components["version"].set_property("pkg_config_name", f"libsisl_version")
        self.cpp_info.components["version"].requires.extend([
                "logging",
                "zmarok-semver::zmarok-semver",
                ])
        self.cpp_info.components["sisl"].libs = [""]
        self.cpp_info.components["sisl"].requires.extend([
                "file_watcher",
                "sobject",
                "version",
                ])
        if self.options.metrics:
            self.cpp_info.components["settings"].libs = ["sisl_settings"]
            self.cpp_info.components["settings"].set_property("pkg_config_name", f"libsisl_settings")
            self.cpp_info.components["settings"].requires.extend([
                    "logging",
                    "flatbuffers::flatbuffers",
                    "userspace-rcu::userspace-rcu",
                    ])
            self.cpp_info.components["metrics"].libs = ["sisl_metrics"]
            self.cpp_info.components["metrics"].set_property("pkg_config_name", f"libsisl_metrics")
            self.cpp_info.components["metrics"].requires.extend([
                    "logging",
                    "folly::folly",
                    "prometheus-cpp::prometheus-cpp",
                    ])
            self.cpp_info.components["buffer"].libs = ["sisl_buffer"]
            self.cpp_info.components["buffer"].set_property("pkg_config_name", f"libsisl_buffer")
            self.cpp_info.components["buffer"].requires.extend([
                    "metrics",
                    "folly::folly",
                    "userspace-rcu::userspace-rcu",
                    ])

            self.cpp_info.components["cache"].libs = ["sisl_cache"]
            self.cpp_info.components["cache"].set_property("pkg_config_name", f"libsisl_cache")
            self.cpp_info.components["cache"].requires.extend([
                    "buffer",
                    ])
            self.cpp_info.components["sisl"].requires.extend([
                    "cache",
                    "settings",
                    ])

        if self.options.grpc:
            self.cpp_info.components["grpc"].libs = ["sisl_grpc"]
            self.cpp_info.components["grpc"].set_property("pkg_config_name", f"libsisl_grpc")
            self.cpp_info.components["grpc"].requires.extend([
                    "buffer",
                    "grpc::grpc",
                    ])
            self.cpp_info.components["flip"].libs = ["flip"]
            self.cpp_info.components["flip"].set_property("pkg_config_name", f"libflip")
            self.cpp_info.components["flip"].requires.extend([
                    "logging",
                    "grpc::grpc",
                    ])
            self.cpp_info.components["sisl"].requires.extend([
                    "grpc",
                    ])

        for component in self.cpp_info.components.values():
            if self.settings.os in ["Linux", "FreeBSD"]:
                component.defines.append("_POSIX_C_SOURCE=200809L")
                component.defines.append("_FILE_OFFSET_BITS=64")
                component.defines.append("_LARGEFILE64")
                component.system_libs.extend(["dl", "pthread"])
                component.exelinkflags.extend(["-export-dynamic"])
            if self.options.get_safe("prerelease") or (self.settings.build_type == "Debug"):
                component.defines.append("_PRERELEASE=1")
            if  self.options.get_safe("sanitize"):
                component.sharedlinkflags.append("-fsanitize=address")
                component.exelinkflags.append("-fsanitize=address")
                component.sharedlinkflags.append("-fsanitize=undefined")
                component.exelinkflags.append("-fsanitize=undefined")
            if self.options.malloc_impl == 'jemalloc':
                component.defines.append("USE_JEMALLOC=1")
                component.requires.extend(["jemalloc::jemalloc"])
            elif self.options.malloc_impl == 'tcmalloc':
                component.defines.append("USING_TCMALLOC=1")
                component.requires.extend(["gperftools::gperftools"])
