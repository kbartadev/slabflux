# -------------------------------------------------------------
# SLABFLUX Benchmarks (Google Benchmark)
# -------------------------------------------------------------

cmake_minimum_required(VERSION 3.20)

include(FetchContent)

# Fetch Google Benchmark
FetchContent_Declare(
    googlebenchmark
    URL https://github.com/google/benchmark/archive/refs/tags/v1.8.3.zip
)

# Disable Google Benchmark�s own GTest tests
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googlebenchmark)

# -------------------------------------------------------------
# Collect all benchmark .cpp files
# -------------------------------------------------------------
file(GLOB SLABFLUX_BENCH_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# Collect all headers to scan for MOC annotations
file(GLOB_RECURSE ENGINE_HEADERS "${CMAKE_SOURCE_DIR}/include/*.hpp")
file(GLOB_RECURSE BENCH_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")

# -------------------------------------------------------------
# Create an executable for each benchmark
# -------------------------------------------------------------
foreach(BENCH_FILE ${SLABFLUX_BENCH_SOURCES})
    get_filename_component(BENCH_TARGET ${BENCH_FILE} NAME_WE)

    set(BENCH_NAME "bm_${BENCH_TARGET}")

    add_executable(${BENCH_NAME} ${BENCH_FILE})

    target_link_libraries(${BENCH_NAME}
    PRIVATE
        benchmark::benchmark_main
        benchmark::benchmark
        slabflux
    )

    if(WIN32)
        target_link_libraries(${BENCH_NAME} PRIVATE ws2_32)
    endif()

    target_compile_features(${BENCH_NAME} PRIVATE cxx_std_20)

    # Scan the benchmark source and all headers for MOC annotations
    slabflux_generate_meta(${BENCH_NAME} "${BENCH_TARGET}_meta.hpp" "${BENCH_FILE}" ${ENGINE_HEADERS} ${BENCH_HEADERS})

    set_property(TARGET ${BENCH_NAME} PROPERTY FOLDER "Benchmarks")

    message(STATUS "SLABFLUX benchmark added: ${BENCH_NAME}")
endforeach()
