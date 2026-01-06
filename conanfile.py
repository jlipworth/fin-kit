import os
from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, cmake_layout


class FinkitConan(ConanFile):
    name = "finkit"
    version = "0.1.0"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps"

    def requirements(self):
        # TODO: Add quantlib/1.30 when Boost compatibility is resolved
        # (Boost 1.80.0 fails with Apple Clang 17)
        self.requires("duckdb/1.4.3")
        self.requires("spdlog/1.17.0")
        # fmt is pulled in by spdlog - don't specify separately to avoid conflicts
        self.requires("nlohmann_json/3.11.3")

    def generate(self):
        tc = CMakeToolchain(self, generator="Ninja")
        # Use LLVM's libc++ instead of system libc++ to avoid ABI issues
        llvm_lib = "/opt/homebrew/opt/llvm/lib/c++"
        if os.path.exists(llvm_lib):
            tc.variables["CMAKE_EXE_LINKER_FLAGS"] = f"-L{llvm_lib} -Wl,-rpath,{llvm_lib}"
            tc.variables["CMAKE_SHARED_LINKER_FLAGS"] = f"-L{llvm_lib} -Wl,-rpath,{llvm_lib}"
        tc.generate()

    def layout(self):
        cmake_layout(self)
