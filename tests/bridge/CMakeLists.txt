# tests/net/CMakeLists.txt
file(GLOB BRIDGE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 1. CREATE INDIVIDUAL TARGETS
foreach(BRIDGE_FILE ${BRIDGE_SOURCES})
    get_filename_component(BRIDGE_NAME ${BRIDGE_FILE} NAME_WE)
    add_executable(${BRIDGE_NAME} ${BRIDGE_FILE} "bridge_sync_test.cpp")
    target_link_libraries(${BRIDGE_NAME} PRIVATE slabflux gtest gtest_main)
    target_compile_features(${BRIDGE_NAME} PRIVATE cxx_std_20)
    gtest_discover_tests(${BRIDGE_NAME} DISCOVERY_TIMEOUT 30)
    set_property(TARGET ${BRIDGE_NAME} PROPERTY FOLDER "Tests/Bridge/Individual")
endforeach()

# 2. CREATE UNIFIED TARGET
set(BRIDGE_UNIFIED all_bridge)
add_executable(${BRIDGE_UNIFIED} ${BRIDGE_SOURCES} "bridge_sync_test.cpp")
target_link_libraries(${BRIDGE_UNIFIED} PRIVATE slabflux gtest gtest_main)
target_compile_features(${BRIDGE_UNIFIED} PRIVATE cxx_std_20)
gtest_discover_tests(${BRIDGE_UNIFIED} DISCOVERY_TIMEOUT 60)
set_property(TARGET ${BRIDGE_UNIFIED} PROPERTY FOLDER "Tests/Bridge")