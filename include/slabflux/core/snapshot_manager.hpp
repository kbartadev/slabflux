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
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <immintrin.h>
#include "slabflux/compute/vector_lane_engine.hpp"

namespace slabflux::core {

    /**
     * @brief Deterministic state freezer.
     * @details Captures the Chip’s current phase space.
     */
    template<typename Engine>
    struct snapshot_manager {
        /**
         * @brief mmap the state for persistence.
         * @details Memory topology is part of the model.
         */
        static void freeze(const Engine& engine, const char* path) noexcept {
            int fd = ::open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
            if (SL_EXPECT_FALSE(fd < 0)) return;

            if (SL_EXPECT_FALSE(::ftruncate(fd, sizeof(Engine)) < 0)) {
                ::close(fd);
                return;
            }

            // MAP_POPULATE forces immediate page table resolution, 
            // preventing page-fault micro-stalls during the copy loop.
            void* map = ::mmap(nullptr, sizeof(Engine), PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, 0);
            if (SL_EXPECT_FALSE(map == MAP_FAILED)) {
                ::close(fd);
                return;
            }

            // Non-Temporal Streaming (Cache Bypass).
            // Prevents the snapshot operation from polluting the L1/L2 cache and evicting hot-path trading data.
            const uint8_t* src = reinterpret_cast<const uint8_t*>(&engine);
            uint8_t* dst = static_cast<uint8_t*>(map);
            size_t offset = 0;

#if defined(__AVX512F__)
            for (; offset + 64 <= sizeof(Engine); offset += 64) {
                __m512i v = _mm512_loadu_si512(src + offset);
                _mm512_stream_si512(reinterpret_cast<__m512i*>(dst + offset), v);
            }
#elif defined(__AVX2__)
            for (; offset + 32 <= sizeof(Engine); offset += 32) {
                __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + offset));
                _mm256_stream_si256(reinterpret_cast<__m256i*>(dst + offset), v);
            }
#endif
            for (; offset < sizeof(Engine); ++offset) dst[offset] = src[offset];

            _mm_sfence(); // Force non-temporal write-combining buffers to flush to physical memory

            // Asynchronous schedule: Prevents the compute core from blocking on NVMe latency
            ::msync(map, sizeof(Engine), MS_ASYNC);
            ::munmap(map, sizeof(Engine));
            ::close(fd);
        }
    };

} // namespace slabflux::core
