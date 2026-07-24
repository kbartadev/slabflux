# Build and Integration

The project's build system is anchored on modern CMake (3.20+) and MSVC, strictly adhering to the "Hermetic Build" principle where static linking is aggressively preferred to eliminate runtime dynamic resolution overhead.

## The CMake Monolith (`tests/CMakeLists.txt`)
The build orchestrator dynamically morphs to fit the target OS topology. Utilizing the `WIN32` guard, incompatible Linux-specific submodules are explicitly purged from the build tree via a robust `REGEX` exclusion list (`LINUX_ONLY_TESTS`) regardless of their physical directory depth (`core/`, `audit/`, `bridge/`):

```cmake
file(GLOB_RECURSE ALL_SLABFLUX_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

if(WIN32)
    set(LINUX_ONLY_TESTS
        "hardware_affinity"
        "industrial_jitter"
        "io_uring"
        # ... etc ...
    )
    foreach(PATTERN IN LISTS LINUX_ONLY_TESTS)
        list(FILTER ALL_SLABFLUX_SOURCES EXCLUDE REGEX ".*${PATTERN}.*")
    endforeach()
endif()
```

## Compiler Flags
To guarantee bit-exact performance parity, the following directives are non-negotiable across all toolchains:

- Strict C++20 compliance (`-std=c++20` or `/std:c++20`).
- Maximum aggressive optimization (`/O2` on MSVC, `-O3` on GCC/Clang).
- Link-Time Optimization (LTO) strictly enabled to flatten cross-translation-unit boundaries.
- Mandatory hardware SIMD vectorization (`/arch:AVX2` or `-mavx2`).
