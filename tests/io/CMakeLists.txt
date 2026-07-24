# tests/io/CMakeLists.txt
file(GLOB IO_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 1. CREATE INDIVIDUAL TARGETS
foreach(IO_FILE ${IO_SOURCES})
    get_filename_component(IO_NAME ${IO_FILE} NAME_WE)

    set(SHOULD_BUILD TRUE)
    
    # Platform Filtering: strictly isolate OS-specific IO engines
    if(IO_NAME MATCHES "rio_.*")
        if(NOT WIN32)
            set(SHOULD_BUILD FALSE)
        endif()
    elseif(IO_NAME MATCHES "uring_.*|xdp_.*")
        if(WIN32)
            set(SHOULD_BUILD FALSE)
        endif()
    endif()

    if(SHOULD_BUILD)
        add_executable(${IO_NAME} ${IO_FILE})
        target_link_libraries(${IO_NAME} PRIVATE slabflux GTest::gtest_main GTest::gmock)
        target_compile_features(${IO_NAME} PRIVATE cxx_std_20)

        # Generate MOC for individual IO tests
        slabflux_generate_meta(${IO_NAME} "${IO_NAME}_moc.hpp" "${IO_FILE}" ${ENGINE_HEADERS} ${TEST_HEADERS})

        gtest_discover_tests(${IO_NAME} DISCOVERY_TIMEOUT 30)
    endif()
endforeach()

# 2. CREATE UNIFIED TARGET
set(IO_UNIFIED all_io)
set(FILTERED_IO_SOURCES "")
foreach(IO_FILE ${IO_SOURCES})
    get_filename_component(IO_NAME ${IO_FILE} NAME_WE)
    set(KEEP_FILE TRUE)

    if(IO_NAME MATCHES "rio_.*")
        if(NOT WIN32)
            set(KEEP_FILE FALSE)
        endif()
    elseif(IO_NAME MATCHES "uring_.*|xdp_.*")
        if(WIN32)
            set(KEEP_FILE FALSE)
        endif()
    endif()

    if(KEEP_FILE)
        list(APPEND FILTERED_IO_SOURCES ${IO_FILE})
    endif()
endforeach()

if(FILTERED_IO_SOURCES)
    add_executable(${IO_UNIFIED} ${FILTERED_IO_SOURCES})
    target_link_libraries(${IO_UNIFIED} PRIVATE slabflux GTest::gtest_main GTest::gmock)
    target_compile_features(${IO_UNIFIED} PRIVATE cxx_std_20)

    # Generate unified MOC typelist for IO tests
    slabflux_generate_meta(${IO_UNIFIED} "${IO_UNIFIED}_moc.hpp" ${FILTERED_IO_SOURCES} ${ENGINE_HEADERS} ${TEST_HEADERS})

    gtest_discover_tests(${IO_UNIFIED} DISCOVERY_TIMEOUT 60)
endif()
