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
#include "slabflux/net/delta_compressor.hpp"
#include <vector>

using namespace slabflux::net;

struct alignas(64) test_payload { 
    float data[16]; 
};

TEST(DeltaCompressorTest, BasicDelta) {
    test_payload current[1024] = {0};
    test_payload previous[1024] = {0};

    current[10].data[0] = 1.0f;
    current[20].data[0] = 2.0f; // This is in a different 16-float (64-byte) block

    delta_block deltas[100];
    size_t count = delta_compressor::generate_delta(current, previous, 1024, deltas);

    EXPECT_EQ(count, 2);
    EXPECT_EQ(deltas[0].offset, 10);
    EXPECT_EQ(deltas[1].offset, 20);

    // Check data in first delta block
    float* delta_data = reinterpret_cast<float*>(deltas[0].data);
    EXPECT_EQ(delta_data[0], 1.0f);
}

TEST(DeltaCompressorTest, NoChange) {
    test_payload current[1024] = {1.0f};
    test_payload previous[1024] = {1.0f};

    delta_block deltas[100];
    size_t count = delta_compressor::generate_delta(current, previous, 1024, deltas);

    EXPECT_EQ(count, 0);
}
