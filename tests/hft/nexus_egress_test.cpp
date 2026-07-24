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

#include <gtest/gtest.h>
#include "slabflux/net/wire_frame_lsn.hpp"

namespace slabflux::security {
    class judge {
    public:
        static bool is_sanctified(uint32_t client_id) noexcept {
            return client_id != 666; // Simple blacklist simulation
        }
    };
}

using namespace slabflux::net;
using namespace slabflux::security;

TEST(Security, JudgeIngressValidation) {
    EXPECT_TRUE(judge::is_sanctified(100));
    EXPECT_FALSE(judge::is_sanctified(666)); // Rogue client blocked
}

TEST(Labs, ZeroCopyEgressAlignment) {
    // Verifies that the egress frame fits perfectly in a cache line
    // for non-temporal streaming stores.
    struct hft_payload { double price; uint64_t size; };
    using hft_frame = wire_frame_lsn<hft_payload>;

    EXPECT_EQ(sizeof(hft_frame), 64);
    EXPECT_EQ(alignof(hft_frame), 64);
}

TEST(Labs, StallFreeNexus) {
    // Pattern for stall_free_nexus.hpp
    std::atomic<uint32_t> bus_pressure{0};

    auto push_signal = [&]() {
        bus_pressure.fetch_add(1, std::memory_order_relaxed);
    };

    push_signal();
    EXPECT_EQ(bus_pressure.load(), 1);
}
