add_library(finkit_viz
    src/viz.cpp
)

target_include_directories(finkit_viz
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(finkit_viz
    PUBLIC finkit::backtest
)

target_compile_features(finkit_viz PUBLIC cxx_std_20)

add_library(finkit::viz ALIAS finkit_viz)
