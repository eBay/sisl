from conan import ConanFile

class PrereleaseConan(ConanFile):
    name = "prerelease_dummy"
    version = "1.0.1"
    homepage = "https://github.corp.ebay.com/SDS/prerelease_dummy"
    description = "A dummy package to invoke PRERELEASE option"
    topics = ("ebay", "nublox")
    url = "https://github.corp.ebay.com/SDS/prerelease_dummy"
    license = "Apache-2.0"

    settings = ()

    exports_sources = ("LICENSE")

    def build(self):
        pass

    def package(self):
        pass

    def package_info(self):
        self.cpp_info.cxxflags.append("-D_PRERELEASE=1")
