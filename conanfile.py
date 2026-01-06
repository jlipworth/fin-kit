from conan import ConanFile
from conan.tools.cmake import cmake_layout


class FinkitConan(ConanFile):
    name = "finkit"
    version = "0.1.0"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"

    def requirements(self):
        self.requires("quantlib/1.33")
        self.requires("duckdb/1.0.0")
        self.requires("spdlog/1.14.1")
        self.requires("fmt/10.2.1")
        self.requires("nlohmann_json/3.11.3")

    def layout(self):
        cmake_layout(self)
