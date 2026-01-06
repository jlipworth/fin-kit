add_library(finkit_analysis
    src/analysis.cpp
)

target_include_directories(finkit_analysis
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(finkit_analysis
    PUBLIC finkit::data
)

target_compile_features(finkit_analysis PUBLIC cxx_std_20)

add_library(finkit::analysis ALIAS finkit_analysis)
