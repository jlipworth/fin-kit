add_library(finkit_core
    src/core.cpp
)

target_include_directories(finkit_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(finkit_core PUBLIC cxx_std_20)

add_library(finkit::core ALIAS finkit_core)
