from conans import ConanFile, CMake, tools

class MetricsConan(ConanFile):
    name = "metrics"
    version = "0.1.0"

    license = "Proprietary"
    url = "https://github.corp.ebay.com/SDS/metrics"
    description = "Metrics collection project for eBay SDS"

    settings = "arch", "os", "compiler", "build_type"
    options = {"shared": ['True', 'False'],
               "fPIC": ['True', 'False']}
    default_options = 'shared=False', 'fPIC=True'

    requires = (("sds_logging/2.1.3@sds/stable"),
                ("gtest/1.8.0@bincrafters/stable"))

    generators = "cmake"
    exports_sources = "CMakeLists.txt", "cmake/*", "src/*"

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
        cmake.test()

    def package(self):
        self.copy("*.hpp", src="src/include", dst="include/{}".format(self.name), keep_path=False)
        self.copy("*.a", dst="lib/", keep_path=False)
        self.copy("*.lib", dst="lib/", keep_path=False)
        self.copy("*.so", dst="lib/", keep_path=False)
        self.copy("*.dll", dst="lib/", keep_path=False)
        self.copy("*.dylib", dst="lib/", keep_path=False)

    def package_info(self):
        self.cpp_info.libs = tools.collect_libs(self)
