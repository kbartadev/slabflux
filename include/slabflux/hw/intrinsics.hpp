/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * ============================================================================
 * SLABFLUX SOFTWARE ENGINE
 * Copyright (c) 2026 Kristóf Barta (https://github.com/kbartadev)
 * ============================================================================
 * SOURCE-AVAILABLE CODEBASE
 *
 * This source file is distributed under the conditions of the SLABFLUX 
 * SOURCE-AVAILABLE AND ECOSYSTEM LICENSE (the "License").
 *
 * ----------------------------------------------------------------------------
 * CRITICAL WARNING
 * ----------------------------------------------------------------------------
 * This module may execute outside standard OS mediation layers. Incorrect 
 * integration, misconfiguration, or unsafe deployment can result in:
 *
 *   • irreversible data corruption
 *   • kernel instability or panics
 *   • NIC or PCIe bus desynchronization
 *   • undefined hardware state transitions
 *   • permanent loss of system integrity
 *
 * Use only in controlled environments with full understanding of the 
 * architectural constraints and hardware implications.
 *
 * ----------------------------------------------------------------------------
 * USAGE GUIDELINES
 * ----------------------------------------------------------------------------
 * Execution, integration, and deployment by developers is permitted strictly 
 * subject to the conditional grants and structural limitations defined within 
 * the License. Please refer to the License for full terms regarding corporate 
 * deployment and replication.
 *
 * ----------------------------------------------------------------------------
 * LIMITATION OF LIABILITY
 * ----------------------------------------------------------------------------
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, IN NO EVENT SHALL THE AUTHOR OR 
 * COPYRIGHT HOLDER BE LIABLE FOR ANY CLAIM, DAMAGES, OR OTHER LIABILITY, 
 * WHETHER IN AN ACTION OF CONTRACT, TORT, OR OTHERWISE, ARISING FROM, OUT OF, 
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * ----------------------------------------------------------------------------
 * DISCLAIMER OF WARRANTY
 * ----------------------------------------------------------------------------
 * THIS SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *
 * See accompanying LICENSE and NOTICE files for the integrated terms of use.
 * ============================================================================*
 * @file       intrinsics.hpp
 * @brief      Cross-platform Hardware Intrinsics and Compiler Directives.
 * @details    Abstracts MSVC / GCC / Clang specific built-ins (tzcnt, popcnt)
 * and loop unrolling pragmas to keep hot-paths clean.
 */

#pragma once

#include <cstdint>

// ============================================================================
// COMPILER SPECIFIC DIRECTIVES (Inline & Unrolling)
// ============================================================================
#if defined(_MSC_VER) && !defined(__clang__)
    // MSVC (Windows)
    #include <intrin.h>
    #include <immintrin.h>

    #define SLAB_UNROLL_8 __pragma(loop(unroll, 8))

#elif defined(__clang__)
    // Clang (Cross-platform)
    #include <immintrin.h>

    #define SLAB_UNROLL_8 _Pragma("unroll 8")

#elif defined(__GNUC__)
    // GCC (Linux)
    #include <immintrin.h>

    #define SLAB_UNROLL_8 _Pragma("GCC unroll 8")

#else
    #error "Unsupported compiler! SLABFLUX requires MSVC, GCC, or Clang."
#endif

// ============================================================================
// HARDWARE INSTRUCTIONS (Zero-Branch Math)
// ============================================================================
namespace slabflux::hw {

    inline int ctz32(uint32_t value) noexcept {
#if defined(_MSC_VER)
        unsigned long trailing_zero = 0;
        if (_BitScanForward(&trailing_zero, value)) return static_cast<int>(trailing_zero);
        return 32;
#else
        return value == 0 ? 32 : __builtin_ctz(value);
#endif
    }

    inline int ctz64(uint64_t value) noexcept {
        if (value == 0) return 64;
#if defined(_MSC_VER)
        unsigned long r = 0;
        _BitScanForward64(&r, value);
        return static_cast<int>(r);
#else
        return __builtin_ctzll(value);
#endif
    }

    /**
     * @brief Counts the number of trailing zero bits (TZCNT/BSF).
     * @warning The input 'mask' MUST NOT be 0, otherwise behavior is undefined.
     */
    inline uint32_t tzcnt_32(uint32_t val) noexcept {
#ifdef _MSC_VER
        unsigned long index;
        return _BitScanForward(&index, val) ? (uint32_t)index : 32;
#else
        return val ? (uint32_t)__builtin_ctz(val) : 32;
#endif
    }

    /**
     * @brief 64-bit trailing zero count.
     */
    inline uint64_t tzcnt_64(uint64_t val) noexcept {
#ifdef _MSC_VER
        unsigned long index;
        return _BitScanForward64(&index, val) ? (uint64_t)index : 64;
#else
        return val ? (uint64_t)__builtin_ctzll(val) : 64;
#endif
    }

    /**
     * Bit Manipulation: Population Count
     */
    inline int popcount64(uint64_t value) noexcept {
#if defined(_MSC_VER)
        return static_cast<int>(__popcnt64(value));
#else
        return __builtin_popcountll(value);
#endif
    }

    /**
     * Cache Control: Prefetching
     */
    enum class prefetch_locality {
        LOW = 0,    // _MM_HINT_T2
        MEDIUM = 1, // _MM_HINT_T1
        HIGH = 2,   // _MM_HINT_T0
        NTA = 3     // _MM_HINT_NTA (Non-Temporal)
    };

    template<prefetch_locality L = prefetch_locality::HIGH>
    inline void prefetch(const void* addr) noexcept {
#if defined(_MSC_VER) || defined(__x86_64__)
        if constexpr (L == prefetch_locality::HIGH) _mm_prefetch(static_cast<const char*>(addr), _MM_HINT_T0);
        else if constexpr (L == prefetch_locality::MEDIUM) _mm_prefetch(static_cast<const char*>(addr), _MM_HINT_T1);
        else if constexpr (L == prefetch_locality::LOW) _mm_prefetch(static_cast<const char*>(addr), _MM_HINT_T2);
        else _mm_prefetch(static_cast<const char*>(addr), _MM_HINT_NTA);
#endif
    }

    /**
     * Spin-loop optimization: Yield to hardware thread
     */
    inline void cpu_pause() noexcept {
#if defined(_MSC_VER) || defined(__x86_64__)
        _mm_pause();
#else
        __builtin_ia32_pause();
#endif
    }

    /**
     * Endianness: Fast byte swap for network order
     */
    inline uint64_t bswap64(uint64_t val) noexcept {
#if defined(_MSC_VER)
        return _byteswap_uint64(val);
#else
        return __builtin_bswap64(val);
#endif
    }

} // namespace slabflux::hw
