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

# Homebrew paths for macOS (Boost only - QuantLib must be built with LLVM
# libc++)
if(APPLE)
  execute_process(
    COMMAND brew --prefix
    OUTPUT_VARIABLE HOMEBREW_PREFIX
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  list(APPEND CMAKE_PREFIX_PATH "${HOMEBREW_PREFIX}")

  # Boost headers for QuantLib (brew install boost)
  execute_process(
    COMMAND brew --prefix boost
    OUTPUT_VARIABLE BOOST_PREFIX
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  set(BOOST_ROOT "${BOOST_PREFIX}")
endif()

# QuantLib built with LLVM libc++ (see scripts/build-quantlib.sh) IMPORTANT:
# Homebrew's QuantLib uses system libc++ which is ABI-incompatible with LLVM's
# libc++. We must use a custom build.
set(QUANTLIB_ROOT
    "$ENV{HOME}/.local/quantlib-llvm"
    CACHE PATH "QuantLib install prefix")
list(PREPEND CMAKE_PREFIX_PATH "${QUANTLIB_ROOT}")
find_package(QuantLib 1.40 REQUIRED)

# Boost headers (required by QuantLib)
find_package(Boost REQUIRED)

# Conan-installed packages
find_package(libpqxx REQUIRED)
find_package(redis++ REQUIRED)
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(tomlplusplus REQUIRED)
find_package(Eigen3 REQUIRED)

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
add_subdirectory(src/types)
add_subdirectory(src/bootstrap)
add_subdirectory(src/basis)
add_subdirectory(src/stats)
add_subdirectory(src/valuation)
add_subdirectory(src/trading)
add_subdirectory(src/risk)
add_subdirectory(src/curves)
add_subdirectory(src/analysis)
add_subdirectory(src/backtest)
add_subdirectory(src/viz)

# Applications
add_subdirectory(apps)

# Services
add_subdirectory(services)

# Tests
add_subdirectory(tests)

# Scratch/validation scripts (untracked, optional)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/scratch/CMakeLists.txt")
  add_subdirectory(scratch)
endif()
