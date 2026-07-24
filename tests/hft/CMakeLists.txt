# tests/hft/CMakeLists.txt
file(GLOB HFT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 1. CREATE INDIVIDUAL TARGETS
foreach(HFT_FILE ${HFT_SOURCES})
    get_filename_component(HFT_NAME ${HFT_FILE} NAME_WE)
    add_executable(${HFT_NAME} ${HFT_FILE})
    target_link_libraries(${HFT_NAME} PRIVATE slabflux gtest gtest_main)
    target_compile_features(${HFT_NAME} PRIVATE cxx_std_20)
    gtest_discover_tests(${HFT_NAME} DISCOVERY_TIMEOUT 30)
    set_property(TARGET ${HFT_NAME} PROPERTY FOLDER "Tests/HFT/Individual")
endforeach()

# 2. CREATE UNIFIED TARGET
set(HFT_UNIFIED all_hft_gateway)
add_executable(${HFT_UNIFIED} ${HFT_SOURCES})
target_link_libraries(${HFT_UNIFIED} PRIVATE slabflux gtest gtest_main)
target_compile_features(${HFT_UNIFIED} PRIVATE cxx_std_20)
gtest_discover_tests(${HFT_UNIFIED} DISCOVERY_TIMEOUT 60)
set_property(TARGET ${HFT_UNIFIED} PROPERTY FOLDER "Tests/HFT")
