add_library(finkit_core)

target_sources(finkit_core PUBLIC FILE_SET CXX_MODULES FILES src/core.cppm)

target_compile_features(finkit_core PUBLIC cxx_std_20)

add_library(finkit::core ALIAS finkit_core)
