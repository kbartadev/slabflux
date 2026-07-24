# tests/net/CMakeLists.txt
file(GLOB COMPUTE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 1. CREATE INDIVIDUAL TARGETS
foreach(COMPUTE_FILE ${COMPUTE_SOURCES})
    get_filename_component(COMPUTE_NAME ${COMPUTE_FILE} NAME_WE)
    add_executable(${COMPUTE_NAME} ${COMPUTE_FILE})
    target_link_libraries(${COMPUTE_NAME} PRIVATE slabflux gtest gtest_main)
    target_compile_features(${COMPUTE_NAME} PRIVATE cxx_std_20)
    gtest_discover_tests(${COMPUTE_NAME} DISCOVERY_TIMEOUT 30)
    set_property(TARGET ${COMPUTE_NAME} PROPERTY FOLDER "Tests/Compute/Individual")
endforeach()

# 2. CREATE UNIFIED TARGET
set(COMPUTE_UNIFIED all_compute)
add_executable(${COMPUTE_UNIFIED} ${COMPUTE_SOURCES})
target_link_libraries(${COMPUTE_UNIFIED} PRIVATE slabflux gtest gtest_main)
target_compile_features(${COMPUTE_UNIFIED} PRIVATE cxx_std_20)
gtest_discover_tests(${COMPUTE_UNIFIED} DISCOVERY_TIMEOUT 60)
set_property(TARGET ${COMPUTE_UNIFIED} PROPERTY FOLDER "Tests/Compute")