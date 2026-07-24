# tests/sys/CMakeLists.txt
file(GLOB SYS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 1. CREATE INDIVIDUAL TARGETS
foreach(SYS_FILE ${SYS_SOURCES})
    get_filename_component(SYS_NAME ${SYS_FILE} NAME_WE)
    add_executable(${SYS_NAME} ${SYS_FILE})
    target_link_libraries(${SYS_NAME} PRIVATE slabflux gtest gtest_main)
    target_compile_features(${SYS_NAME} PRIVATE cxx_std_20)
    gtest_discover_tests(${SYS_NAME} DISCOVERY_TIMEOUT 30)
    set_property(TARGET ${SYS_NAME} PROPERTY FOLDER "Tests/Sys/Individual")
endforeach()

# 2. CREATE UNIFIED TARGET
set(SYS_UNIFIED all_sys)
add_executable(${SYS_UNIFIED} ${SYS_SOURCES})
target_link_libraries(${SYS_UNIFIED} PRIVATE slabflux gtest gtest_main)
target_compile_features(${SYS_UNIFIED} PRIVATE cxx_std_20)
gtest_discover_tests(${SYS_UNIFIED} DISCOVERY_TIMEOUT 60)
set_property(TARGET ${SYS_UNIFIED} PROPERTY FOLDER "Tests/Sys")
