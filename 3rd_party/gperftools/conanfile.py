#!/usr/bin/env python
# -*- coding: utf-8 -*-

from conans import ConanFile, AutoToolsBuildEnvironment, tools

class GPerfToolsConan(ConanFile):
    name = "gperftools"
    version = "2.7.0"
    release = "2.7"
    license = "BSD"

    description = "A portable library to determine the call-chain of a C program"
    settings = "os", "arch", "compiler", "build_type"

    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = "shared=False", "fPIC=True"

    requires = (("xz_utils/5.2.4"))

    generators = "compiler_args"

    def source(self):
        source_url = "https://github.com/{0}/{0}/releases/download".format(self.name)
        tools.get("{0}/{1}-{2}/{1}-{2}.tar.gz".format(source_url, self.name, self.release))

    def build(self):
        env_build = AutoToolsBuildEnvironment(self)
        env_build.cxx_flags.append("@conanbuildinfo.args")
        if self.settings.build_type != "Debug":
            env_build.defines.append('NDEBUG')
        configure_args = ['--disable-dependency-tracking', '--enable-libunwind']
        if self.options.shared:
            configure_args += ['--enable-shared=yes', '--enable-static=no']
        else:
            configure_args += ['--enable-shared=no', '--enable-static=yes']
        env_build.configure(args=configure_args,configure_dir="{0}-{1}".format(self.name, self.release))
        env_build.make(args=["-j1"])

    def package(self):
        headers = ['heap-checker.h', 'heap-profiler.h', 'malloc_extension.h', 'malloc_extension_c.h',
                   'malloc_hook.h', 'malloc_hook_c.h', 'profiler.h', 'stacktrace.h', 'tcmalloc.h']
        for header in headers:
            self.copy("*{0}".format(header), dst="include/google", src="{0}-{1}/src/google".format(self.name, self.release), keep_path=False)
            self.copy("*{0}".format(header), dst="include/gperftools", src="{0}-{1}/src/gperftools".format(self.name, self.release), keep_path=False)
        self.copy("*.so*", dst="lib", keep_path=False, symlinks=True)
        self.copy("*.a", dst="lib", keep_path=False, symlinks=True)

    def package_info(self):
        self.cpp_info.libs = ['tcmalloc_minimal']
