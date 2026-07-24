cmake_minimum_required(VERSION 3.20)

project(slabflux
    VERSION 1.0.0
    DESCRIPTION "SLABFLUX"
    LANGUAGES CXX
)

# -------------------------------------------------------------
# Global configuration
# -------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SLABFLUX_ENABLE_META_COMPILER "Enable Meta Compiler generation" ON)

if(NOT SLABFLUX_ENABLE_META_COMPILER)
    add_compile_definitions(SLABFLUX_SKIP_META_COMPILER)
    message(STATUS "SlabFlux Meta Compiler: DISABLED (Using pure SFINAE duck-typing fallback)")
else()
    message(STATUS "SlabFlux Meta Compiler: ENABLED")
endif()

# -------------------------------------------------------------
# Header-only SLABFLUX library
# -------------------------------------------------------------
add_library(slabflux INTERFACE)

target_include_directories(slabflux INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# -------------------------------------------------------------
# Metadata Compiler
# -------------------------------------------------------------
if(SLABFLUX_ENABLE_META_COMPILER)
add_executable(slabflux_meta src/compiler/meta_compiler.cpp)
target_compile_features(slabflux_meta PRIVATE cxx_std_20)

# -------------------------------------------------------------
# Metadata Compiler Validation
# -------------------------------------------------------------
add_executable(meta_compiler_test src/compiler/meta_compiler_test.cpp)
target_link_libraries(meta_compiler_test PRIVATE GTest::gtest_main GTest::gtest)
add_test(NAME MetaCompilerValidation COMMAND meta_compiler_test)
endif()

# -------------------------------------------------------------
# METADATA Generation Function for Downstream Users
# -------------------------------------------------------------
include(cmake/slabflux_meta.cmake)

if(NOT SLABFLUX_ENABLE_META_COMPILER)
    # Override the meta generation macro to generate a harmless dummy header.
    # This ensures perfect compatibility with OFF mode by preventing missing header errors.
    function(slabflux_generate_meta TARGET_NAME OUTPUT_HEADER)
        set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/slabflux_generated")
        set(GEN_FILE "${GEN_DIR}/${OUTPUT_HEADER}")
        file(MAKE_DIRECTORY "${GEN_DIR}")
        file(WRITE "${GEN_FILE}" "/* SLABFLUX FALLBACK META DUMMY */\n#pragma once\n")
        target_include_directories(${TARGET_NAME} PRIVATE "${GEN_DIR}")
    endfunction()
endif()

if(WIN32)
    # On Windows we remove the networking and Linux‑specific examples
    list(REMOVE_ITEM PROJECT_SOURCES
        "src/io/io_uring_ingress.cpp"
    )
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    find_package(PkgConfig REQUIRED)

    # Enforce global static lookup within pkg-config evaluation contexts
    set(PKG_CONFIG_USE_CMAKE_PREFIX_PATH ON)

    pkg_check_modules(LIBBPF REQUIRED libbpf)
    pkg_check_modules(LIBXDP REQUIRED libxdp)
    pkg_check_modules(LIBELF REQUIRED libelf)
    pkg_check_modules(LIBDW REQUIRED libdw)

    # 1. Search for internal elfutils components (libasm/libdwelf)
    # These often contain the 'eu_' symbols needed by libelf.a
    pkg_check_modules(LIBASM libasm)
    pkg_check_modules(LIBDWELF libdwelf)

    # 2. Transitive compression dependencies required by modern libelf/libdw static archives
    pkg_check_modules(LIBZSTD REQUIRED libzstd)
    pkg_check_modules(LIBBZ2 REQUIRED bzip2)
    pkg_check_modules(LIBLZMA REQUIRED liblzma) # For elf compression
    pkg_check_modules(LIBZ REQUIRED zlib)

    # 1. Probe for DPDK using PkgConfig framework
    pkg_check_modules(DPDK libdpdk)

    # 2. Strict fallback mapping for modern Debian architecture layouts
    if(NOT DPDK_FOUND)
        set(DPDK_INCLUDE_DIRS
            "/usr/include/dpdk"
            "/usr/include/x86_64-linux-gnu/dpdk"
        )
        set(DPDK_LIBRARIES
            dpdk
            rte_eal
            rte_ethdev
            rte_mbuf
            rte_mempool
            rte_ring
        )
    endif()

    # 3. Bind discovered include paths to interface boundary
    target_include_directories(slabflux SYSTEM INTERFACE
        ${LIBBPF_INCLUDE_DIRS}
        ${LIBXDP_INCLUDE_DIRS}
        ${LIBELF_INCLUDE_DIRS}
        ${LIBASM_INCLUDE_DIRS}
        ${LIBDWELF_INCLUDE_DIRS}
        ${LIBDW_INCLUDE_DIRS}
        ${LIBZSTD_INCLUDE_DIRS}
        ${LIBBZ2_INCLUDE_DIRS}
        ${LIBLZMA_INCLUDE_DIRS}
        ${LIBZ_INCLUDE_DIRS}
        ${DPDK_INCLUDE_DIRS}
    )

    # 4. Synthesize all performance primitives for user-space networking
    target_link_libraries(slabflux INTERFACE
        "-Wl,--start-group"
        ${LIBXDP_LIBRARIES}
        ${LIBBPF_LIBRARIES}
        ${LIBDW_LIBRARIES}
        ${LIBELF_LIBRARIES}
        ${LIBASM_LIBRARIES}
        ${LIBDWELF_LIBRARIES}
        uring
        ${LIBZSTD_LIBRARIES}
        ${LIBBZ2_LIBRARIES}
        ${LIBLZMA_LIBRARIES}
        ${LIBZ_LIBRARIES}
        ${DPDK_LIBRARIES}
        numa
        "-Wl,--end-group"
        rt
        pthread
        dl
    )

    target_compile_definitions(slabflux INTERFACE SLABFLUX_USE_IO_URING)

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(slabflux INTERFACE -march=native -O3)
    endif()
endif()

# -------------------------------------------------------------
# System Runners
# -------------------------------------------------------------
add_executable(ai_system_runner src/ai_system_runner.cpp)
target_link_libraries(ai_system_runner PRIVATE slabflux)

# -------------------------------------------------------------
# Examples
# -------------------------------------------------------------
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt")
    add_subdirectory(examples)
endif()

# -------------------------------------------------------------
# Tests
# -------------------------------------------------------------
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif()

# -------------------------------------------------------------
# MSVC Native CPU Optimization
# -------------------------------------------------------------
if (MSVC)
    if (CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(
            /O2         # Full optimization
            /GL         # Whole Program Optimization (LTO)
            /arch:AVX2  # Modern SIMD
            /fp:fast    # Fast floating point
        )
        add_link_options(/LTCG)  # Link-time code generation
    endif()
endif()

# The Hermetic Seal
# Enforces static linking for absolute sovereignty over the runtime.

# set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++ -static")

# Force all internal dependencies to be static
# set(BUILD_SHARED_LIBS OFF)

# Embedding configuration into the binary (X-Macro or Resource Embedding)
add_custom_command(
    OUTPUT server_cert.o client_cert.o
    COMMAND objcopy -I binary -O elf64-x86-64 -B i386 server_cert.der server_cert.o
    COMMAND objcopy -I binary -O elf64-x86-64 -B i386 client_cert.der client_cert.o
    DEPENDS server_cert.der client_cert.der
)
# Note: Bind the generated objects to your target (e.g., target_sources(slabflux PRIVATE server_cert.o client_cert.o))

# Final Flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-stack-protector -fno-plt")

# -------------------------------------------------------------
# Benchmarks
# -------------------------------------------------------------
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/CMakeLists.txt")
    enable_testing()
    add_subdirectory(benchmarks)
endif()

# -------------------------------------------------------------
# Installation
# -------------------------------------------------------------
install(TARGETS slabflux
    EXPORT slabfluxTargets
    INCLUDES DESTINATION include
)

install(DIRECTORY include/ DESTINATION include)

install(EXPORT slabfluxTargets
    FILE slabflux_targets.cmake
    NAMESPACE slabflux::
    DESTINATION lib/cmake/slabflux
)

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/slabflux_config_version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/slabflux_config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/slabflux_config.cmake"
    INSTALL_DESTINATION lib/cmake/slabflux
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/slabflux_config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/slabflux_config_version.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/slabflux_meta.cmake"
    DESTINATION lib/cmake/slabflux
)

if(CMAKE_EXPORT_COMPILE_COMMANDS)
    add_custom_command(
        OUTPUT "${CMAKE_SOURCE_DIR}/compile_commands.json"
        COMMAND ${CMAKE_COMMAND} -E create_symlink
                "${CMAKE_BINARY_DIR}/compile_commands.json"
                "${CMAKE_SOURCE_DIR}/compile_commands.json"
    )
    add_custom_target(copy_compile_commands ALL
        DEPENDS "${CMAKE_SOURCE_DIR}/compile_commands.json")
endif()
