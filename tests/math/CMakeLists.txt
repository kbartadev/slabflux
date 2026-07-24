# tests/math/CMakeLists.txt
file(GLOB MATH_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 1. CREATE INDIVIDUAL TARGETS
foreach(MATH_FILE ${MATH_SOURCES})
    get_filename_component(MATH_NAME ${MATH_FILE} NAME_WE)
    add_executable(${MATH_NAME} ${MATH_FILE})
    target_link_libraries(${MATH_NAME} PRIVATE slabflux gtest gtest_main)
    target_compile_features(${MATH_NAME} PRIVATE cxx_std_20)
    gtest_discover_tests(${MATH_NAME} DISCOVERY_TIMEOUT 30)
    set_property(TARGET ${MATH_NAME} PROPERTY FOLDER "Tests/Math/Individual")
endforeach()

# 2. CREATE UNIFIED TARGET
set(MATH_UNIFIED all_math)
add_executable(${MATH_UNIFIED} ${MATH_SOURCES})
target_link_libraries(${MATH_UNIFIED} PRIVATE slabflux gtest gtest_main)
target_compile_features(${MATH_UNIFIED} PRIVATE cxx_std_20)
gtest_discover_tests(${MATH_UNIFIED} DISCOVERY_TIMEOUT 60)
set_property(TARGET ${MATH_UNIFIED} PROPERTY FOLDER "Tests/Math")
