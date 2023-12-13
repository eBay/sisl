from os.path import join
from conan import ConanFile
from conan.tools.files import copy
from conan.tools.build import check_min_cppstd
from conans import CMake

required_conan_version = ">=1.52.0"

class SISLConan(ConanFile):
    name = "sisl"
    version = "11.0.2"

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
                'testing' : ['True', 'False'],
                "sanitize": ['True', 'False'],
                'prerelease' : ['True', 'False'],
                'malloc_impl' : ['libc', 'tcmalloc', 'jemalloc'],
              }
    default_options = {
                'shared': False,
                'fPIC': True,
                'coverage': False,
                'testing': True,
                'sanitize': False,
                'prerelease': False,
                'malloc_impl': 'libc',
            }

    generators = "cmake", "cmake_find_package"
    exports = ["LICENSE"]
    exports_sources = (
                "CMakeLists.txt",
                "cmake/*",
                "include/*",
                "src/*",
            )

    def validate(self):
        if self.info.settings.compiler.cppstd:
            check_min_cppstd(self, 20)

    def configure(self):
        if self.settings.compiler in ["gcc"]:
            self.options['pistache'].with_ssl: True
        if self.options.shared:
            del self.options.fPIC
        if self.settings.build_type == "Debug":
            self.options.prerelease = True
            if self.options.coverage and self.options.sanitize:
                raise ConanInvalidConfiguration("Sanitizer does not work with Code Coverage!")
            if not self.options.testing:
                if self.options.coverage or self.options.sanitize:
                    raise ConanInvalidConfiguration("Coverage/Sanitizer requires Testing!")

    def build_requirements(self):
        self.build_requires("benchmark/1.8.2")
        self.build_requires("cmake/3.27.0")
        self.build_requires("gtest/1.14.0")

    def requirements(self):
        # Custom packages
        if self.options.prerelease:
            self.requires("prerelease_dummy/1.0.1")

        # Memory allocation
        if self.options.malloc_impl == "tcmalloc":
            self.requires("gperftools/2.7.0")
        elif self.options.malloc_impl == "jemalloc":
            self.requires("jemalloc/5.2.1")

        # Linux Specific Support
        if self.settings.os in ["Linux"]:
            self.requires("folly/nu2.2023.12.11.00")
            self.requires("userspace-rcu/0.11.4")

        # Generic packages (conan-center)
        self.requires("boost/1.82.0")
        if self.settings.os in ["Linux"]:
            self.requires("breakpad/cci.20230127")
        self.requires("cxxopts/3.1.1")
        self.requires("flatbuffers/23.5.26")
        self.requires("grpc/1.50.1")
        self.requires("nlohmann_json/3.11.2")
        self.requires("prometheus-cpp/1.1.0")
        self.requires("spdlog/1.12.0")
        self.requires("zmarok-semver/1.1.0")
        self.requires("fmt/10.0.0",     override=True)
        self.requires("libcurl/8.2.1",  override=True)
        self.requires("openssl/3.1.3",  override=True)
        self.requires("xz_utils/5.2.5", override=True)
        self.requires("zlib/1.2.13",    override=True)

    def build(self):
        cmake = CMake(self)

        definitions = {'CONAN_BUILD_COVERAGE': 'OFF',
                       'ENABLE_TESTING': 'OFF',
                       'CMAKE_EXPORT_COMPILE_COMMANDS': 'ON',
                       'CONAN_CMAKE_SILENT_OUTPUT': 'ON',
                       'MEMORY_SANITIZER_ON': 'OFF',
                       'MALLOC_IMPL': self.options.malloc_impl}

        if self.settings.build_type == "Debug":
            if self.options.sanitize:
                definitions['MEMORY_SANITIZER_ON'] = 'ON'
            elif self.options.coverage:
                definitions['CONAN_BUILD_COVERAGE'] = 'ON'

        if self.options.testing:
            definitions['ENABLE_TESTING'] = 'ON'

        cmake.configure(defs=definitions)
        cmake.build()
        if self.options.testing:
            cmake.test(output_on_failure=True)

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
