# Collect all .cpp example files
file(GLOB_RECURSE SLAB_EXAMPLE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

foreach(SRC_FILE ${SLAB_EXAMPLE_FILES})
    # Get the file name without extension (e.g., 01_cascading_pipeline)
    get_filename_component(BASE_NAME ${SRC_FILE} NAME_WE)
    
    # FILTER: Skip Linux-only files on Windows
    if(WIN32 AND (BASE_NAME STREQUAL "chip_genesis" OR BASE_NAME MATCHES ".*kernel.*"))
        continue()
    endif()

    # SOLUTION: Use the relative path to create a unique target name
    # This prevents "Duplicate Target" errors if files have same names in different folders
    file(RELATIVE_PATH REL_PATH "${CMAKE_CURRENT_SOURCE_DIR}" ${SRC_FILE})
    get_filename_component(REL_DIR ${REL_PATH} DIRECTORY)
    
    # Replace slashes with underscores to make a valid, unique CMake target name
    string(REPLACE "/" "_" DIR_PREFIX "${REL_DIR}")
    if(DIR_PREFIX)
        set(TARGET_NAME "ex_${DIR_PREFIX}_${BASE_NAME}")
    else()
        set(TARGET_NAME "ex_${BASE_NAME}")
    endif()

    # Only add if it doesn't exist yet
    if(NOT TARGET ${TARGET_NAME})
        add_executable(${TARGET_NAME} ${SRC_FILE})
        target_link_libraries(${TARGET_NAME} PRIVATE slabflux)
        target_compile_features(${TARGET_NAME} PRIVATE cxx_std_20)
        
        if(WIN32)
            target_compile_definitions(${TARGET_NAME} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        endif()
        
        message(STATUS "Added Example: ${TARGET_NAME}")
    endif()
endforeach()
