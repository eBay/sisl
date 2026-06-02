import os

from conan import ConanFile
from conan.tools.files import copy, get
from conan.tools.layout import basic_layout

required_conan_version = ">=2.0"


class StdexecConan(ConanFile):
    name = "stdexec"
    description = "NVIDIA reference implementation of std::execution (P2300): senders/receivers + coroutines"
    license = "Apache-2.0 WITH LLVM-exception"
    url = "https://github.com/NVIDIA/stdexec"
    homepage = "https://github.com/NVIDIA/stdexec"
    topics = ("execution", "p2300", "senders", "coroutines", "header-only")

    # Header-only: the package is one binary for all settings (see package_id), but we keep
    # settings so package_info() can branch on the consumer's compiler for warning flags.
    package_type = "header-library"
    settings = "os", "arch", "compiler", "build_type"
    no_copy_source = True

    def layout(self):
        basic_layout(self, src_folder="src")

    def package_id(self):
        self.info.clear()

    def source(self):
        get(self, **self.conan_data["sources"][self.version], strip_root=True)

    def package(self):
        copy(self, "LICENSE*", src=self.source_folder,
             dst=os.path.join(self.package_folder, "licenses"))
        copy(self, "*", src=os.path.join(self.source_folder, "include"),
             dst=os.path.join(self.package_folder, "include"))

    def package_info(self):
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []
        # stdexec's <execution> include pulls libstdc++'s PSTL, which defaults to the TBB backend;
        # force the serial backend so consumers don't have to link TBB. Mirrors the in-tree
        # stdexec_iface targets in iomgr/nuraft_mesg/homestore.
        self.cpp_info.defines = ["_PSTL_PAR_BACKEND_SERIAL"]
        # stdexec internals trip a couple of warnings; suppress so -Werror consumers still build.
        # -Wsubobject-linkage is gcc-only -- clang rejects it as an unknown warning option.
        compiler = str(self.settings.compiler)
        if compiler == "gcc":
            self.cpp_info.cxxflags = ["-Wno-empty-body", "-Wno-subobject-linkage"]
        elif compiler == "clang":
            self.cpp_info.cxxflags = ["-Wno-empty-body"]
