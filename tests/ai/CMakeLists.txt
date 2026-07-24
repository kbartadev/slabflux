# tests/domain/CMakeLists.txt
file(GLOB AI_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 1. CREATE INDIVIDUAL TARGETS
foreach(AI_FILE ${AI_SOURCES})
    get_filename_component(AI_NAME ${AI_FILE} NAME_WE)
    add_executable(${AI_NAME} ${AI_FILE})
    target_link_libraries(${AI_NAME} PRIVATE slabflux gtest gtest_main)
    target_compile_features(${AI_NAME} PRIVATE cxx_std_20)
    gtest_discover_tests(${AI_NAME} DISCOVERY_TIMEOUT 30)
    set_property(TARGET ${AI_NAME} PROPERTY FOLDER "Tests/AI_Domain/Individual")
endforeach()

# 2. CREATE UNIFIED TARGET
set(AI_UNIFIED all_moe_domain)
add_executable(${AI_UNIFIED} ${AI_SOURCES})
target_link_libraries(${AI_UNIFIED} PRIVATE slabflux gtest gtest_main)
target_compile_features(${AI_UNIFIED} PRIVATE cxx_std_20)
gtest_discover_tests(${AI_UNIFIED} DISCOVERY_TIMEOUT 60)
set_property(TARGET ${AI_UNIFIED} PROPERTY FOLDER "Tests/AI_Domain")
