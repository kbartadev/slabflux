# tests/net/CMakeLists.txt
file(GLOB TRANSPORT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 1. CREATE INDIVIDUAL TARGETS
foreach(TRANSPORT_FILE ${TRANSPORT_SOURCES})
    get_filename_component(TRANSPORT_NAME ${TRANSPORT_FILE} NAME_WE)
    add_executable(${TRANSPORT_NAME} ${TRANSPORT_FILE})
    target_link_libraries(${TRANSPORT_NAME} PRIVATE slabflux gtest gtest_main)
    target_compile_features(${TRANSPORT_NAME} PRIVATE cxx_std_20)
    gtest_discover_tests(${TRANSPORT_NAME} DISCOVERY_TIMEOUT 30)
    set_property(TARGET ${TRANSPORT_NAME} PROPERTY FOLDER "Tests/Transport/Individual")
endforeach()

# 2. CREATE UNIFIED TARGET
set(TRANSPORT_UNIFIED all_transport)
add_executable(${TRANSPORT_UNIFIED} ${TRANSPORT_SOURCES})
target_link_libraries(${TRANSPORT_UNIFIED} PRIVATE slabflux gtest gtest_main)
target_compile_features(${TRANSPORT_UNIFIED} PRIVATE cxx_std_20)
gtest_discover_tests(${TRANSPORT_UNIFIED} DISCOVERY_TIMEOUT 60)
set_property(TARGET ${TRANSPORT_UNIFIED} PROPERTY FOLDER "Tests/Transport")