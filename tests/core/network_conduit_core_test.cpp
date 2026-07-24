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
 * ============================================================================* @brief SLABFLUX - Network Conduit Architecture Audit
 */

#ifndef _WIN32
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#endif

#include <gtest/gtest.h>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/network_conduit.hpp"

using namespace slabflux::core;

struct mock_packet {
    size_t size_bytes{64};
    char data[64];
};

struct mock_pool {
    void release(mock_packet*) {}
};

TEST(NetworkConduitTest, RingBufferSymmetry) {
    spsc_conduit<mock_packet*, 16> ring;
    mock_packet p1, p2;
    mock_packet* out = nullptr;
    
    ASSERT_TRUE(ring.try_push(&p1));
    ASSERT_TRUE(ring.try_push(&p2));
    
    ASSERT_TRUE(ring.try_pop(out)); EXPECT_EQ(out, &p1);
    ASSERT_TRUE(ring.try_pop(out)); EXPECT_EQ(out, &p2);
    EXPECT_FALSE(ring.try_pop(out));
}

TEST(NetworkConduitTest, SenderNodeBackpressure) {
    mock_pool pool;
    std::atomic<bool> running{true};
    spsc_conduit<mock_packet*, 4> conduit;
    // Mock raw_tcp_frame for mem_pool.release
    transport::raw_tcp_frame mock_raw_frame;
    mock_raw_frame.payload_length = 64;
    std::memcpy(mock_raw_frame.data, "mock_data", 9);
    
    // Create a dummy socket for the gateway. We need a valid socket to initialize
    // standard_egress_gateway, but its behavior is mocked by the test setup.
    int dummy_sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_NE(dummy_sock, -1);
    // Set non-blocking to ensure send() can return EWOULDBLOCK
    int flags = fcntl(dummy_sock, F_GETFL, 0);
    ASSERT_NE(flags, -1);
    ASSERT_NE(fcntl(dummy_sock, F_SETFL, flags | O_NONBLOCK), -1);

    standard_egress_gateway<spsc_conduit<mock_packet*, 4>, mock_pool, 4> egress_gateway(dummy_sock, conduit, pool, running);
    mock_packet p1, p2, p3, p4;
    
    // Fill the conduit to capacity
    EXPECT_TRUE(conduit.try_push(&p1));
    EXPECT_TRUE(conduit.try_push(&p2));
    EXPECT_TRUE(conduit.try_push(&p3));
    EXPECT_TRUE(conduit.try_push(&p4));
    EXPECT_EQ(conduit.occupancy(), 4);
    
    // Drive the egress gateway to process the frames
    // This will attempt to send the 4 packets. If the kernel buffer is full,
    // send() will return EWOULDBLOCK, and the packets will be reverted to the conduit.
    // If some are sent, they are released from the conduit.
    egress_gateway.poll_egress();

    // After poll_egress, the conduit's occupancy might be less than 4 if some packets were sent.
    // The test should verify that try_push behaves correctly based on the *actual* occupancy.
    mock_packet p5;
    if (conduit.occupancy() == 4) {
        // If the conduit is still full (all sends failed and reverted), try_push should fail.
        EXPECT_FALSE(conduit.try_push(&p5));
    } else {
        // If some packets were sent, space is available, try_push should succeed.
        EXPECT_TRUE(conduit.try_push(&p5));
        // To clean up, pop the pushed item.
        mock_packet* out_p5;
        ASSERT_TRUE(conduit.try_pop(out_p5));
    }
    
    close(dummy_sock);
}

TEST(NetworkConduitTest, PhysicalAlignment) {
    spsc_conduit<uint64_t, 1024> ring;
    
    // Verify that the structure doesn't violate 64-byte alignment
    // which would cause false-sharing between Compute and I/O threads.
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&ring) % 64, 0);
    EXPECT_EQ(sizeof(ring) % 64, 0);
}