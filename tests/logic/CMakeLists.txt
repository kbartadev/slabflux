# tests/logic/CMakeLists.txt
file(GLOB LOGIC_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 1. CREATE INDIVIDUAL TARGETS
foreach(LOGIC_FILE ${LOGIC_SOURCES})
    get_filename_component(LOGIC_NAME ${LOGIC_FILE} NAME_WE)
    add_executable(${LOGIC_NAME} ${LOGIC_FILE})
    target_link_libraries(${LOGIC_NAME} PRIVATE slabflux gtest gtest_main)
    target_compile_features(${LOGIC_NAME} PRIVATE cxx_std_20)
    gtest_discover_tests(${LOGIC_NAME} DISCOVERY_TIMEOUT 30)
    set_property(TARGET ${LOGIC_NAME} PROPERTY FOLDER "Tests/Logic/Individual")
endforeach()

# 2. CREATE UNIFIED TARGET
set(LOGIC_UNIFIED all_logic)
add_executable(${LOGIC_UNIFIED} ${LOGIC_SOURCES})
target_link_libraries(${LOGIC_UNIFIED} PRIVATE slabflux gtest gtest_main)
target_compile_features(${LOGIC_UNIFIED} PRIVATE cxx_std_20)
gtest_discover_tests(${LOGIC_UNIFIED} DISCOVERY_TIMEOUT 60)
set_property(TARGET ${LOGIC_UNIFIED} PROPERTY FOLDER "Tests/Logic")
