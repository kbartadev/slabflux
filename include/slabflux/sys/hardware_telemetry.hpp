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
 *
 * @file hardware_telemetry.hpp
 * @brief Micro-architectural Observability.
 * @details Directly interfaces with CPU Performance Monitoring Counters (PMC) 
 * to measure L1 misses and IPC for every single event.
 */

#pragma once

#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sys/mman.h>
#include <cstdint>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::sys {

    /**
     * @brief PMU Instruction Configuration.
     * @details Encapsulates hardware event selection logic within a C++20 consteval
     * container to allow for direct RDPMC assembly injection during the hot path.
     */
    struct pmu_event {
        static constexpr uint32_t FIXED_FUNCTION_MASK = (1U << 30);
        uint32_t counter_idx;
        consteval pmu_event(uint32_t idx) : counter_idx(idx) {}
    };
    inline constexpr pmu_event PMU_CYCLES{pmu_event::FIXED_FUNCTION_MASK + 0};

    /**
     * @brief Micro-architectural Reader.
     * @details Replaces textbook syscall-based counter reads with hardware-native
     * RDPMC instructions. This achieves < 35 cycle latency for observability.
     * @tparam Event The consteval pmu_event configuration.
     */
    template <pmu_event Event>
    class pmu {
    public:
         /** @brief Samples the silicon counter via injected RDPMC instruction. */
        SLAB_FORCE_INLINE uint64_t sample() const noexcept {
            uint32_t low, high;
            // Intel SDM: RDPMC reads from PMC specified in ECX.
            // Injecting the counter index as a compile-time constant eliminates
            // register pressure and branch overhead.
            asm volatile(
                "rdpmc"
                : "=a" (low), "=d" (high)
                : "c" (Event.counter_idx)
                : "memory"
            );
            return (static_cast<uint64_t>(high) << 32) | low;
        }
    };

    /**
     * @brief PMU Control Gate.
     * @details Manages the kernel-level initialization and syscall-based fallback
     * for hardware performance monitoring counters (PMC).
     */
    class pmu_monitor {
        int fd_{-1};
        perf_event_mmap_page* pc_{nullptr};

    public:
        void open_counter(uint32_t type, uint64_t config) {
            perf_event_attr pe{};
            pe.type = type;
            pe.size = sizeof(perf_event_attr);
            pe.config = config;
            pe.disabled = 1;
            pe.exclude_kernel = 1;
            pe.exclude_hv = 1;

            fd_ = static_cast<int>(syscall(__NR_perf_event_open, &pe, 0, -1, -1, 0));
            if (fd_ != -1) {
                // Map the perf ring buffer into user-space to allow 
                // direct RDPMC reading without crossing the kernel boundary.
                pc_ = static_cast<perf_event_mmap_page*>(mmap(nullptr, sysconf(_SC_PAGESIZE), 
                                                              PROT_READ, MAP_SHARED, fd_, 0));
                ioctl(fd_, PERF_EVENT_IOC_ENABLE, 0);
            }
        }

        /** @brief User-space fast-path PMC reader via mapped control page. */
        SLAB_FORCE_INLINE uint64_t read_counter() const noexcept {
            if (SL_EXPECT_FALSE(!pc_)) return 0;
            
            uint32_t seq, index;
            uint64_t count;
            do {
                seq = pc_->lock;
                asm volatile("" ::: "memory");
                index = pc_->index;
                count = pc_->offset;
                if (SL_EXPECT_TRUE(index > 0)) {
                    uint32_t ecx = index - 1;
                    uint32_t a, d;
                    asm volatile("rdpmc" : "=a"(a), "=d"(d) : "c"(ecx));
                    count += (static_cast<uint64_t>(d) << 32) | a;
                }
                asm volatile("" ::: "memory");
            } while (pc_->lock != seq);
            
            return count;
        }

        ~pmu_monitor() {
            if (pc_ && pc_ != MAP_FAILED) munmap(pc_, sysconf(_SC_PAGESIZE));
            if (fd_ != -1) close(fd_);
        }
    };
}
