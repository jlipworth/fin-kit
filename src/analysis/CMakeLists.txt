add_library(finkit_analysis)

target_sources(finkit_analysis PUBLIC FILE_SET CXX_MODULES FILES analysis.cppm)

target_link_libraries(finkit_analysis PUBLIC finkit::data)

target_compile_features(finkit_analysis PUBLIC cxx_std_20)

add_library(finkit::analysis ALIAS finkit_analysis)
