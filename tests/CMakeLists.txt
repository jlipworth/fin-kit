find_package(Catch2 REQUIRED)

include(CTest)
include(Catch)

add_subdirectory(core)
add_subdirectory(data)
add_subdirectory(analysis)
add_subdirectory(backtest)
add_subdirectory(viz)
