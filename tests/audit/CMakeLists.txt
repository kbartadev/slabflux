file(GLOB AUDIT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# Define files that are physically impossible to compile on native Windows
if(WIN32)
    set(LINUX_ONLY_TESTS
        "${CMAKE_CURRENT_SOURCE_DIR}/industrial_jitter_test.cpp"
    )
    # Remove Linux-specific files from the source list
    list(REMOVE_ITEM AUDIT_SOURCES ${LINUX_ONLY_TESTS})
    message(STATUS "[SLABFLUX] Skipping Linux-specific audit tests on Windows")
endif()

# 1. Individual Audit Targets
foreach(RT_FILE ${AUDIT_SOURCES})
    get_filename_component(RT_NAME ${RT_FILE} NAME_WE)
    add_executable(${RT_NAME} ${RT_FILE})
    target_link_libraries(${RT_NAME} PRIVATE slabflux gtest gtest_main)
    target_compile_features(${RT_NAME} PRIVATE cxx_std_20)
    
    # Discovery timeout increased for Windows CI environments
    gtest_discover_tests(${RT_NAME} DISCOVERY_TIMEOUT 60)
    set_property(TARGET ${RT_NAME} PROPERTY FOLDER "Tests/Audit/Individual")
endforeach()

# 2. Unified Audit Target
if(AUDIT_SOURCES)
    set(AUDIT_UNIFIED all_audit)
    add_executable(${AUDIT_UNIFIED} ${AUDIT_SOURCES})
    target_link_libraries(${AUDIT_UNIFIED} PRIVATE slabflux gtest gtest_main)
    target_compile_features(${AUDIT_UNIFIED} PRIVATE cxx_std_20)
    gtest_discover_tests(${AUDIT_UNIFIED} DISCOVERY_TIMEOUT 60)
    set_property(TARGET ${AUDIT_UNIFIED} PROPERTY FOLDER "Tests/Audit")
endif()