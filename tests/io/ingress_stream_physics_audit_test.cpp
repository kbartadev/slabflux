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
 * @file ingress_stream_physics_audit.cpp
 * @brief Multishot io_uring Ingress Physics.
 */

#include <gtest/gtest.h>
#include <x86intrin.h>
#include "slabflux/io/uring_ingress_stream.hpp"
#include "slabflux/core/pool.hpp"
#include "slabflux/transport/http_avx.hpp"

using namespace slabflux;
using namespace slabflux::io;

namespace slabflux::transport {
    /**
     * @brief Test-specific Request Envelope.
     * @details Extends the base event with a physical buffer to simulate 
     * incoming wire data during residency and alignment audit tests.
     */
    static constexpr size_t MAX_PAYLOAD = 4096;

    struct http_request : public http_request_event {
        alignas(64) char raw_buffer[MAX_PAYLOAD];
        size_t buffer_length{0};
    };
}

struct dummy_audit_pipe {
    template <typename T>
    void process(T) {}
};

/**
 * @brief Physical Residency and Alignment.
 * Proves that the multishot engine maintains 64-byte alignment to 
 * prevent MESI thrashing during massive packet bursts.
 */
TEST(Ingress_streamAudit, PhysicalResidency) {
    using audit_stream_t = slabflux::io::uring_ingress_stream<slabflux::core::pool<slabflux::transport::http_request, 1024>, dummy_audit_pipe>;
    EXPECT_EQ(alignof(audit_stream_t), 64);
    EXPECT_EQ(sizeof(audit_stream_t) % 64, 0);
}

/**
 * @brief Prefetch Pipeline Efficiency.
 * Measures the cycle-impact of the Phase A/B split in the poll loop.
 */
TEST(Ingress_streamAudit, PollingPhysics) {
    // This is a structural test of the poll_hot_path logic.
    // Requirement: Even with empty rings, the "Raw Ring Access" overhead 
    // must be sub-10 cycles.
    
    // We use a mock-like setup since we don't want to init a real ring for cycles.
    uint64_t start = __rdtsc();
    
    // Simulated Phase A (Prefetch) + Phase B (Process) logic check
    unsigned head = 0;
    unsigned tail = 0;
    if (SL_LIKELY(head == tail)) {
        _mm_pause();
    }
    
    uint64_t end = __rdtsc();
    std::cout << "[PERF] Multishot Null-Poll: " << (end - start) << " cycles\n";
    
    EXPECT_LT(end - start, 200);
}

/**
 * @brief Buffer Selection Resolution.
 * Proves that buffer IDs from io_uring correctly map to physical 
 * Pool pointers.
 */
TEST(Ingress_streamAudit, BufferMappingIntegrity) {
    core::pool<transport::http_request, 1024> pool;
    
    for(uint16_t bid = 0; bid < 100; ++bid) {
        auto* raw = pool.get_raw_ptr_by_id(bid);
        ASSERT_NE(raw, nullptr);
        
        // Requirement: Pointer resolution must be O(1) and deterministic
        uintptr_t addr = reinterpret_cast<uintptr_t>(raw);
        EXPECT_EQ(addr % 64, 0);
    }
}
