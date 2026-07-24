# tests/CMakeLists.txt
cmake_minimum_required(VERSION 3.20)

include(FetchContent)
include(GoogleTest)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

# Define patterns for platform-specific tests
set(WINDOWS_ONLY_PATTERNS
    "rio_ingress_test"
    "rio_egress_test"
    "rio_duplex_test"
)

set(LINUX_ONLY_PATTERNS
    "telemetry_integrity_test"
    "hardware_affinity_test"
    "matrix_nexus_mock_test"
    "failover_orchestrator_test"
    "bridge_sequence_test"
    "authoritative_bridge_test"
    "persistence_parity_test"
    "hardware_affinity"
    "physical_variants"
    "industrial_jitter"
    "journal_crash_consistency"
    "journal_enospc"
    "memory_pinning_failure"
    "af_xdp_"
    "uring_"
)

# 0. Collect all headers to scan for MOC annotations
file(GLOB_RECURSE ENGINE_HEADERS "${CMAKE_SOURCE_DIR}/include/*.hpp")
file(GLOB_RECURSE TEST_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")

# 1. Collect Core Sources (files in the tests/ directory)
file(GLOB CORE_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.cpp")

# 2. CREATE INDIVIDUAL TARGETS
foreach(TEST_FILE ${CORE_SOURCES})
    get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)

    set(SHOULD_BUILD TRUE)
    
    # Check if this is a Windows-only test
    foreach(PATTERN IN LISTS WINDOWS_ONLY_PATTERNS)
        if("${TEST_NAME}" MATCHES ".*${PATTERN}.*")
            if(NOT WIN32)
                set(SHOULD_BUILD FALSE)
            endif()
            break()
        endif()
    endforeach()
    
    # Check if this is a Linux-only test
    foreach(PATTERN IN LISTS LINUX_ONLY_PATTERNS)
        if("${TEST_NAME}" MATCHES ".*${PATTERN}.*")
            if(WIN32)
                set(SHOULD_BUILD FALSE)
            endif()
            break()
        endif()
    endforeach()

    if(SHOULD_BUILD)
        add_executable(${TEST_NAME} ${TEST_FILE})
        target_link_libraries(${TEST_NAME} PRIVATE slabflux GTest::gtest GTest::gtest_main GTest::gmock)
        target_compile_features(${TEST_NAME} PRIVATE cxx_std_20)
        
        # Generate meta or dummy fallback headers
        slabflux_generate_meta(${TEST_NAME} "${TEST_NAME}_meta.hpp" "${CMAKE_CURRENT_SOURCE_DIR}/${TEST_FILE}" ${ENGINE_HEADERS} ${TEST_HEADERS})
        
        gtest_discover_tests(${TEST_NAME} DISCOVERY_TIMEOUT 20)
        set_property(TARGET ${TEST_NAME} PROPERTY FOLDER "Tests/Core/Individual")
    endif()
endforeach()

# 3. CREATE UNIFIED TARGET (For "Run All")
file(GLOB_RECURSE ALL_SLABFLUX_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(FILTER ALL_SLABFLUX_SOURCES EXCLUDE REGEX ".*fuzz/.*")

set(FILTERED_SOURCES "")
foreach(SRC_PATH ${ALL_SLABFLUX_SOURCES})
    get_filename_component(SRC_NAME ${SRC_PATH} NAME)
    set(KEEP_FILE TRUE)

    # Filter Windows-only files on non-Windows
    if(NOT WIN32)
        foreach(PATTERN IN LISTS WINDOWS_ONLY_PATTERNS)
            if(SRC_NAME MATCHES ".*${PATTERN}.*")
                set(KEEP_FILE FALSE)
                break()
            endif()
        endforeach()
    endif()

    # Filter Linux-only files on non-Linux
    if(WIN32)
        foreach(PATTERN IN LISTS LINUX_ONLY_PATTERNS)
            if(SRC_NAME MATCHES ".*${PATTERN}.*")
                set(KEEP_FILE FALSE)
                break()
            endif()
        endforeach()
    endif()

    if(KEEP_FILE)
        list(APPEND FILTERED_SOURCES ${SRC_PATH})
    endif()
endforeach()

set(CORE_UNIFIED all_slabflux)
if(FILTERED_SOURCES)
    add_executable(${CORE_UNIFIED} ${FILTERED_SOURCES})
    target_link_libraries(${CORE_UNIFIED} PRIVATE slabflux GTest::gtest GTest::gtest_main GTest::gmock)
    target_compile_features(${CORE_UNIFIED} PRIVATE cxx_std_20)
    
    # Generate unified meta or dummy fallback headers
    slabflux_generate_meta(${CORE_UNIFIED} "all_slabflux_meta.hpp" ${FILTERED_SOURCES} ${ENGINE_HEADERS} ${TEST_HEADERS})
    
    gtest_discover_tests(${CORE_UNIFIED} DISCOVERY_TIMEOUT 120)
    set_property(TARGET ${CORE_UNIFIED} PROPERTY FOLDER "Tests/Monolithic")
endif()

# 4. Descend into subdirectories
add_subdirectory(net)
add_subdirectory(transport)
add_subdirectory(runtime)
add_subdirectory(core)
add_subdirectory(compute)
add_subdirectory(bridge)
add_subdirectory(audit)

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/verification/CMakeLists.txt")
    add_subdirectory(verification)
endif()
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/formal/CMakeLists.txt")
    add_subdirectory(formal)
endif()

add_subdirectory(ai)
add_subdirectory(hft)
add_subdirectory(logic)
add_subdirectory(math)
add_subdirectory(io)
add_subdirectory(sys)
add_subdirectory(rte)
