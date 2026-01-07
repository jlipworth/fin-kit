add_library(finkit_data)

target_sources(finkit_data PUBLIC FILE_SET CXX_MODULES FILES data.cppm)

target_link_libraries(finkit_data PUBLIC finkit::core duckdb::duckdb
                                         tomlplusplus::tomlplusplus)

target_compile_features(finkit_data PUBLIC cxx_std_20)

add_library(finkit::data ALIAS finkit_data)
