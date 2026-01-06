add_library(finkit_data
    src/data.cpp
)

target_include_directories(finkit_data
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(finkit_data
    PUBLIC finkit::core
)

target_compile_features(finkit_data PUBLIC cxx_std_20)

add_library(finkit::data ALIAS finkit_data)
