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
 */
#pragma once
#include <immintrin.h>
#include <fcntl.h>
#include <unistd.h>
#include <mutex>

namespace slabflux::sys {

class power_governor {
public:
    enum class status {
        primed,
        unsupported
    };

    /**
     * @brief Primes AVX-512 lanes to avoid frequency scaling penalties.
     * @details Executes dummy vector operations to warm up execution units.
     */
    status prime_vector_units() noexcept {
        // Runtime feature probing to prevent illegal instruction faults on legacy hardware
#if defined(__AVX512F__)
        if (__builtin_cpu_supports("avx512f")) {
            alignas(64) float dummy[16] = {0};
            __m512 zero = _mm512_setzero_ps();
            _mm512_store_ps(dummy, zero);
            asm volatile("" : : "m"(dummy) : "memory");
            return status::primed;
        }
#endif
        // Fallback to AVX2 warming for older high-performance machines (Haswell+)
        if (__builtin_cpu_supports("avx2")) {
            alignas(32) float dummy[8] = {0};
            __m256 zero = _mm256_setzero_ps();
            _mm256_store_ps(dummy, zero);
            asm volatile("" : : "m"(dummy) : "memory");
            return status::primed;
        }
        return status::unsupported;
    }

    /**
     * @brief Static entry point for SIMD priming as expected by the test suite.
     */
    static void warm_up_simd() noexcept {
        power_governor gov;
        gov.prime_vector_units();
    }

    /**
     * @brief Locks C-states by opening /dev/cpu_dma_latency.
     * @details Writing 0 to this file prevents the CPU from entering deep sleep.
     * The file descriptor must remain open for the duration of the lock.
     */
    static void lock_c_states() noexcept {
        static int latency_fd = -1;
        static std::mutex init_mutex;
        std::lock_guard<std::mutex> lock(init_mutex);

        if (latency_fd < 0) {
            latency_fd = open("/dev/cpu_dma_latency", O_WRONLY);
        }
        if (latency_fd >= 0) {
            int32_t latency = 0;
            if (write(latency_fd, &latency, sizeof(latency)) != sizeof(latency)) {
                // In production, log warning: Failed to enforce C-state lock
            }
        }
    }
};

} // namespace slabflux::sys
