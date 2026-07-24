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
 * ============================================================================* @brief SLABFLUX - TCP Stream Fragmenter Audit
 */

#include <gtest/gtest.h>
#include <string_view>
#include "slabflux/transport/tcp_stream_fragmenter.hpp"
#include "slabflux/core/spsc_conduit.hpp"
#include "slabflux/core/mpmc_pool.hpp"
#include "slabflux/transport/wire_protocol.hpp" // Added for raw_egress_packet definition

using namespace slabflux;

TEST(TcpStreamFragmenterTest, FragmentationIntegrity) {
    using NetBus = core::spsc_conduit<core::tagged_pointer, 1024>;
    using PacketPool = core::mpmc_pool<transport::raw_egress_packet, 16>;
    NetBus conduit;
    PacketPool pool;
    transport::tcp_stream_fragmenter<NetBus, PacketPool> fragmenter(conduit, pool);
    
    std::string large_data(3000, 'X');
    
    // Should create 3 fragments (1460 + 1460 + 80)
    fragmenter.fragment_and_push(1, 100, large_data);
    
    EXPECT_EQ(conduit.occupancy(), 3);
    
    core::tagged_pointer tokens[3];
    conduit.pop_batch(tokens, 3);
    
    auto* p1 = reinterpret_cast<transport::raw_egress_packet*>(tokens[0].get_address());
    auto* p3 = reinterpret_cast<transport::raw_egress_packet*>(tokens[2].get_address());
    
    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p3, nullptr);
    
    EXPECT_EQ(p1->header.frame_length, 1460);
    EXPECT_EQ(p1->header.is_last, 0);
    EXPECT_EQ(p3->header.frame_length, 80);
    EXPECT_EQ(p3->header.is_last, 1);
}