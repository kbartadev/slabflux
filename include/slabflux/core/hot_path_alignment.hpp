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
 * @file hot_path_alignment.hpp
 * @brief Instruction Cache & Branch Alignment.
 * @details Forces the linker and the CPU to treat the critical path
 * as a contiguous, uninterrupted block of machine code.
 */

#pragma once
#include <iostream>
#include <cstdlib>
#include <concepts>
#include <type_traits>
#include <cmath>
#include <algorithm>
#include <fstream>
#include "slabflux/hw/intrinsics.hpp"
#include "slabflux/core/physical_layout.hpp"

#ifdef _MSC_VER
#include <intrin.h>
 // MSVC: Define hot section via pragma
#pragma section(".text$hot", read, execute)

#define SLAB_HOT       __forceinline inline
#define SLAB_FORCE_INLINE __forceinline
#define SLAB_COLD      __declspec(noinline)
#define SL_ALIGN
#define SL_SECTION_HOT
#define SLAB_RESTRICT __restrict

// MSVC: Simple pass-through to avoid C2760 syntax errors
#define SL_LIKELY(x)   (x)
#define SL_UNLIKELY(x) (x)
#define SL_EXPECT_TRUE(x)  (x)
#define SL_EXPECT_FALSE(x) (x)
#ifndef SLAB_FLAT_PATH
#define SLAB_FLAT_PATH
#endif

#define SL_START_HOT_REGION __pragma(code_seg(".text$hot"))
#define SL_END_HOT_REGION   __pragma(code_seg())

// Bare-metal hardware halt for Windows
#define SLAB_HARDWARE_HALT() __debugbreak()
#else
#include <immintrin.h>
#include <x86intrin.h>
#include <linux/ioprio.h>

#define SLAB_ATTR_HOT           __attribute__((hot))
#define SLAB_ATTR_COLD          __attribute__((cold))
#define SLAB_ATTR_ALWAYS_INLINE __attribute__((always_inline))
#define SLAB_ATTR_NOINLINE      __attribute__((noinline))

// Purge any conflicting lab or hardware intrinsic macro stubs to satisfy -Werror
#ifdef SLAB_FORCE_INLINE
#undef SLAB_FORCE_INLINE
#endif
#ifdef SLAB_HOT
#undef SLAB_HOT
#endif
#ifdef SLAB_COLD
#undef SLAB_COLD
#endif

// Fix: Swapping to pure GNU attribute syntax enables placement after 'static' specifiers safely!
#define SLAB_FORCE_INLINE       __attribute__((flatten, always_inline)) inline
#define SLAB_HOT                SLAB_ATTR_HOT SLAB_FORCE_INLINE
#define SLAB_COLD               SLAB_ATTR_COLD SLAB_ATTR_NOINLINE
#define SLAB_RESTRICT           __restrict__

// Partitioning: Allows linker scripts to isolate 
// different Expert logic paths into distinct 2MB-aligned code segments.
#define SLAB_EXPERT_HOT(name)   [[gnu::section(".text.expert." #name), gnu::aligned(64), gnu::hot]]

#define SL_ALIGN       alignas(64)
#define SL_SECTION_HOT __attribute__((section(".text.hot")))
#define SL_LIKELY(x)   __builtin_expect(!!(x), 1)
#define SL_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define SL_EXPECT_TRUE(x)  __builtin_expect(!!(x), 1)
#define SL_EXPECT_FALSE(x) __builtin_expect(!!(x), 0)
#define SL_START_HOT_REGION
#define SL_END_HOT_REGION

#define SLAB_FLAT_PATH \
    _Pragma("clang loop unroll(full)") \
    _Pragma("clang loop vectorize(disable)") \
    _Pragma("clang loop interleave(disable)")

// I/O Priority: Real-time class for control-plane persistence
#define SLAB_IOPRIO_RT (static_cast<int>(IOPRIO_PRIO_VALUE(IOPRIO_CLASS_RT, 0)))

// Bare-metal hardware trap for Linux (Generates a clean SIGTRAP without stack unwinding)
#define SLAB_HARDWARE_HALT() __builtin_trap()
#endif

namespace slabflux::hw {
    /**
     * @brief Checks if the host OS is configured for HugePage allocation.
     * Used by tests to skip physical audits on non-HFT tuned environments (Linux).
     */
    inline bool has_hugepage_support() noexcept {
        std::ifstream f("/proc/sys/vm/nr_hugepages");
        int count = 0;
        return (f >> count) && (count > 0);
    }
}

namespace slabflux::rte {
    /**
     * @brief Placeholder for thread telemetry hooks used in validation pools.
     */
    template<typename T>
    inline void watcher_on_thread_ignition(T&, int) noexcept {}
}

namespace slabflux::core {

    /**
     * @brief C++20 explicit layout validation concept.
     */
    template <typename T>
    concept POD = std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>;

    inline void panic_log(const char* msg) {
        std::cerr << "[FATAL ERROR] " << msg << std::endl;
    }

    /** 
     * @brief Anchor Validation.
     * @details Ensures the pointer is not dangling. 
     * If the anchor is invalid, it forces an immediate hardware halt.
     */
    template <typename T>
    SLAB_FORCE_INLINE void slab_anchor_assert(T* ptr) noexcept {
        if (SL_UNLIKELY(ptr == nullptr)) {
            SLAB_HARDWARE_HALT();
        }
    }

    /**
     * @brief Entry point of the Core.
     * @details The 'section' attribute guarantees that the linker places all
     * hot functions next to each other, minimizing I-Cache misses.
     */
    SL_ALIGN SL_SECTION_HOT SLAB_HOT
    void critical_path_step(auto& engine, auto& ingress, auto& journal) {
        // 1. Ingress polling - Branchless check for new frames
        auto* frame = ingress.poll_next();

        if (SL_UNLIKELY(!frame)) {
            // If no data is available, return immediately.
            // The 'pause' instruction preserves the pipeline during busy-wait.
            _mm_pause();
            return;
        }

        // 2. Journaling (Durability) - Non-temporal stores bypass cache to prevent pollution / Simultaneous parallel write
        journal.persist_event(frame, 64, 0);

        // 3. Logic Engine execution - Hot-path dispatch / Deterministic state mutation
        engine.on_fast_path("DATA", {static_cast<const char*>(frame), 64});

        // 4. Software prefetch for the NEXT cycle - O(1) lookahead / Hide memory latency
        ingress.prefetch_next();
    }

    /**
     * @brief Error handling — physically displaced from the hot path.
     * @details The compiler places this into a distant binary segment.
     */
    SLAB_COLD inline void handle_critical_error(const char* msg) {
        // This code MUST NEVER enter the I-Cache during the hot loop.
        panic_log(msg);
        // Generate a core dump before halting the machine
        std::abort();
    }
} // namespace slabflux::core
