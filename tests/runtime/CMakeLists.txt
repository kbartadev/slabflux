file(GLOB RUNTIME_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# Define files that are physically impossible to compile on native Windows
if(WIN32)
    set(LINUX_ONLY_TESTS
        "${CMAKE_CURRENT_SOURCE_DIR}/telemetry_integrity_test.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/hardware_affinity_test.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/matrix_nexus_mock_test.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/failover_orchestrator_test.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/bridge_sequence_test.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/authoritative_bridge_test.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/persistence_parity_test.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/journal_crash_consistency_test.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/journal_enospc_test.cpp"
    )
    # Remove Linux-specific files from the source list
    list(REMOVE_ITEM RUNTIME_SOURCES ${LINUX_ONLY_TESTS})
    message(STATUS "[SLABFLUX] Skipping Linux-specific runtime tests on Windows")
endif()

# 1. Individual Runtime Targets
foreach(RT_FILE ${RUNTIME_SOURCES})
    get_filename_component(RT_NAME ${RT_FILE} NAME_WE)
    add_executable(${RT_NAME} ${RT_FILE})
    target_link_libraries(${RT_NAME} PRIVATE slabflux gtest gtest_main)
    target_compile_features(${RT_NAME} PRIVATE cxx_std_20)
    
    # Discovery timeout increased for Windows CI environments
    gtest_discover_tests(${RT_NAME} DISCOVERY_TIMEOUT 60)
    set_property(TARGET ${RT_NAME} PROPERTY FOLDER "Tests/Runtime/Individual")
endforeach()

# 2. Unified Runtime Target
if(RUNTIME_SOURCES)
    set(RUNTIME_UNIFIED all_runtime)
    add_executable(${RUNTIME_UNIFIED} ${RUNTIME_SOURCES})
    target_link_libraries(${RUNTIME_UNIFIED} PRIVATE slabflux gtest gtest_main)
    target_compile_features(${RUNTIME_UNIFIED} PRIVATE cxx_std_20)
    gtest_discover_tests(${RUNTIME_UNIFIED} DISCOVERY_TIMEOUT 60)
    set_property(TARGET ${RUNTIME_UNIFIED} PROPERTY FOLDER "Tests/Runtime")
endif()