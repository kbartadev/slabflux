# tests/net/CMakeLists.txt
file(GLOB NET_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 1. CREATE INDIVIDUAL TARGETS
foreach(NET_FILE ${NET_SOURCES})
    get_filename_component(NET_NAME ${NET_FILE} NAME_WE)
    add_executable(${NET_NAME} ${NET_FILE})
    target_link_libraries(${NET_NAME} PRIVATE slabflux GTest::gtest GTest::gtest_main GTest::gmock)
    target_compile_features(${NET_NAME} PRIVATE cxx_std_20)
    gtest_discover_tests(${NET_NAME} DISCOVERY_TIMEOUT 30)
    set_property(TARGET ${NET_NAME} PROPERTY FOLDER "Tests/Net/Individual")
endforeach()

# 2. CREATE UNIFIED TARGET
set(NET_UNIFIED all_net)
add_executable(${NET_UNIFIED} ${NET_SOURCES})
target_link_libraries(${NET_UNIFIED} PRIVATE slabflux GTest::gtest GTest::gtest_main GTest::gmock)
target_compile_features(${NET_UNIFIED} PRIVATE cxx_std_20)
gtest_discover_tests(${NET_UNIFIED} DISCOVERY_TIMEOUT 60)
set_property(TARGET ${NET_UNIFIED} PROPERTY FOLDER "Tests/Net")
