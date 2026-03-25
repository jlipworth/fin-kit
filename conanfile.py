import os
from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, cmake_layout


class FinkitConan(ConanFile):
    name = "finkit"
    version = "0.1.0"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps"

    def requirements(self):
        # QuantLib is built separately via scripts/build-quantlib.sh and found
        # by CMake at ~/.local/quantlib-llvm. Do not use Homebrew/system
        # QuantLib for fin-kit; it is ABI-incompatible with the LLVM libc++
        # toolchain used here.
        self.requires("libpqxx/7.9.2")
        self.requires("redis-plus-plus/1.3.15")
        self.requires("spdlog/1.17.0")
        # fmt is pulled in by spdlog - don't specify separately to avoid conflicts
        self.requires("nlohmann_json/3.11.3")
        self.requires("tomlplusplus/3.4.0")
        self.requires("eigen/3.4.0")  # Linear algebra (covariance, factor models, Monte Carlo)

    def generate(self):
        tc = CMakeToolchain(self, generator="Ninja")
        # Use LLVM's libc++ instead of system libc++ to avoid ABI issues
        # Platform-specific paths for LLVM
        import platform
        if platform.system() == "Darwin":
            llvm_lib = "/opt/homebrew/opt/llvm/lib/c++"
        elif platform.system() == "Linux":
            llvm_lib = "/home/linuxbrew/.linuxbrew/opt/llvm/lib"
        else:
            llvm_lib = None

        if llvm_lib and os.path.exists(llvm_lib):
            tc.variables["CMAKE_EXE_LINKER_FLAGS"] = f"-L{llvm_lib} -Wl,-rpath,{llvm_lib}"
            tc.variables["CMAKE_SHARED_LINKER_FLAGS"] = f"-L{llvm_lib} -Wl,-rpath,{llvm_lib}"
        tc.generate()

    def layout(self):
        cmake_layout(self)
