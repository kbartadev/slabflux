file(GLOB CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# Define files that are physically impossible to compile on native Windows
if(WIN32)
    set(LINUX_ONLY_TESTS
        "${CMAKE_CURRENT_SOURCE_DIR}/hardware_affinity_test.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/physical_variants_test.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/memory_pinning_failure_test.cpp"
    )
    # Remove Linux-specific files from the source list
    list(REMOVE_ITEM CORE_SOURCES ${LINUX_ONLY_TESTS})
    message(STATUS "[SLABFLUX] Skipping Linux-specific core tests on Windows")
endif()

# 1. Individual Core Targets
foreach(RT_FILE ${CORE_SOURCES})
    get_filename_component(RT_NAME ${RT_FILE} NAME_WE)
    add_executable(${RT_NAME} ${RT_FILE})
    target_link_libraries(${RT_NAME} PRIVATE slabflux GTest::gtest GTest::gtest_main GTest::gmock)
    target_compile_features(${RT_NAME} PRIVATE cxx_std_20)

    # Generate MOC for individual core tests
    slabflux_generate_meta(${RT_NAME} "${RT_NAME}_moc.hpp" "${RT_FILE}" ${ENGINE_HEADERS} ${TEST_HEADERS})

    # Discovery timeout increased for Windows CI environments
    gtest_discover_tests(${RT_NAME} DISCOVERY_TIMEOUT 60)
    set_property(TARGET ${RT_NAME} PROPERTY FOLDER "Tests/Core/Individual")
endforeach()

# 2. Unified Core Target
if(CORE_SOURCES)
    set(CORE_UNIFIED all_core)
    add_executable(${CORE_UNIFIED} ${CORE_SOURCES})
    target_link_libraries(${CORE_UNIFIED} PRIVATE slabflux GTest::gtest GTest::gtest_main GTest::gmock)
    target_compile_features(${CORE_UNIFIED} PRIVATE cxx_std_20)

    # Generate unified MOC typelist for core tests
    slabflux_generate_meta(${CORE_UNIFIED} "${CORE_UNIFIED}_moc.hpp" ${CORE_SOURCES} ${ENGINE_HEADERS} ${TEST_HEADERS})

    gtest_discover_tests(${CORE_UNIFIED} DISCOVERY_TIMEOUT 60)
    set_property(TARGET ${CORE_UNIFIED} PROPERTY FOLDER "Tests/Core")
endif()
