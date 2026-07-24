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
 * ============================================================================*/


#pragma once

#include <atomic>
#include <cstring>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::compute {

    /**
     * @brief High-Fidelity State Observer.
     * @details Non-intrusive observation.
     * Uses explicit cache-line isolation to prevent MESI contention
     * between the Compute and Management planes.
     */
    template<typename T>
    class alignas(64) sovereign_observer {
        static_assert(std::is_trivially_copyable_v<T>, "Violation: Observer data must be POD");

    private:
        // The Counter sits on its own line.
        alignas(64) std::atomic<uint64_t> seq_{ 0 };

        // The Data is physically separated to avoid False Sharing.
        alignas(64) T data_;

    public:
        /**
         * @brief Writer (Compute Thread): Zero-Stall update.
         */
        SLAB_HOT void update(const T& new_data) noexcept {
            const uint64_t s = seq_.load(std::memory_order_relaxed);

            // 1. Mark update start (Even -> Odd)
            seq_.store(s + 1, std::memory_order_release);

            // 2. Optimized bit-copy
            // Using __builtin_memcpy to allow the compiler to use SIMD (AVX-512)
            __builtin_memcpy(&data_, &new_data, sizeof(T));

            // 3. Mark update end (Odd -> Even)
            seq_.store(s + 2, std::memory_order_release);
        }

        /**
         * @brief Reader (Platform Thread): Wait-free optimistic read.
         */
        SLAB_HOT bool try_read(T& output) const noexcept {
            uint64_t s1, s2;

            // Hardware prefetch hint. 
            // We tell the CPU to bring 'data_' into L1 before we check the sequence.
            _mm_prefetch(reinterpret_cast<const char*>(&data_), _MM_HINT_T0);

            s1 = seq_.load(std::memory_order_acquire);
            if (SL_EXPECT_FALSE(s1 & 1)) return false; // Writer is currently busy

            __builtin_memcpy(&output, &data_, sizeof(T));

            s2 = seq_.load(std::memory_order_acquire);

            return (s1 == s2);
        }
    };
}
