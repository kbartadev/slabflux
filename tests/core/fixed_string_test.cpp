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
#include "slabflux/core/fixed_string.hpp"

using namespace slabflux::core;

TEST(FixedString, VerifiesTriviallyCopyableForNetworkTransfer) {
    // Must be memcpy-safe for network_conduit
    EXPECT_TRUE(std::is_trivially_copyable_v<fixed_string<64>>) 
        << "[FATAL] fixed_string violates trivial copy semantics!";
}

TEST(FixedString, HandlesSafeTruncation) {
    fixed_string<10> fs;
    std::string overflow_data = "THIS_IS_TOO_LONG_FOR_THE_BUFFER";
    
    fs.assign(overflow_data);
    
    // Capacity is 10, meaning exactly 10 characters + 1 null terminator
    EXPECT_EQ(fs.size(), 10);
    EXPECT_EQ(fs.view(), "THIS_IS_TO");
    EXPECT_STREQ(fs.c_str(), "THIS_IS_TO");
}

TEST(FixedString, AssignmentAndViewConsistency) {
    fixed_string<32> fs("192.168.1.100");
    
    EXPECT_EQ(fs.size(), 13);
    EXPECT_FALSE(fs.empty());
    EXPECT_EQ(fs.view(), "192.168.1.100");

    fs.clear();
    EXPECT_TRUE(fs.empty());
    EXPECT_EQ(fs.size(), 0);
}