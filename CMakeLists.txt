cmake_minimum_required(VERSION 3.28)
project(
  finkit
  VERSION 0.1.0
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# C++20 modules support
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  add_compile_options(-Wall -Wextra -Wpedantic -Werror=return-type)
endif()

# Homebrew paths for macOS (QuantLib, Boost)
if(APPLE)
  execute_process(
    COMMAND brew --prefix
    OUTPUT_VARIABLE HOMEBREW_PREFIX
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  list(APPEND CMAKE_PREFIX_PATH "${HOMEBREW_PREFIX}")
  set(ENV{PKG_CONFIG_PATH}
      "${HOMEBREW_PREFIX}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
endif()

# QuantLib from Homebrew via pkg-config (brew install quantlib)
find_package(PkgConfig REQUIRED)
pkg_check_modules(QuantLib REQUIRED IMPORTED_TARGET quantlib)

# Conan-installed packages
find_package(duckdb REQUIRED)
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(tomlplusplus REQUIRED)

# GoogleTest via FetchContent
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0)
# cmake-lint: disable=C0103
set(gtest_force_shared_crt
    ON
    CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

# Source modules
add_subdirectory(src/core)
add_subdirectory(src/data)
add_subdirectory(src/analysis)
add_subdirectory(src/backtest)
add_subdirectory(src/viz)

# Applications
add_subdirectory(apps)

# Tests
add_subdirectory(tests)
