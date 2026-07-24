# tests/rte/CMakeLists.txt
file(GLOB RTE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 1. CREATE INDIVIDUAL TARGETS
foreach(RTE_FILE ${RTE_SOURCES})
    get_filename_component(RTE_NAME ${RTE_FILE} NAME_WE)
    add_executable(${RTE_NAME} ${RTE_FILE})
    target_link_libraries(${RTE_NAME} PRIVATE slabflux gtest gtest_main)
    target_compile_features(${RTE_NAME} PRIVATE cxx_std_20)
    gtest_discover_tests(${RTE_NAME} DISCOVERY_TIMEOUT 30)
    set_property(TARGET ${RTE_NAME} PROPERTY FOLDER "Tests/RTE/Individual")
endforeach()

# 2. CREATE UNIFIED TARGET
set(RTE_UNIFIED all_rte)
add_executable(${RTE_UNIFIED} ${RTE_SOURCES})
target_link_libraries(${RTE_UNIFIED} PRIVATE slabflux gtest gtest_main)
target_compile_features(${RTE_UNIFIED} PRIVATE cxx_std_20)
gtest_discover_tests(${RTE_UNIFIED} DISCOVERY_TIMEOUT 60)
set_property(TARGET ${RTE_UNIFIED} PROPERTY FOLDER "Tests/RTE")
