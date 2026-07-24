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

#include <type_traits>

#include "slabflux/core.hpp"

using namespace slabflux;

struct dummy_event {
    int data;
};

// ============================================================================
// 1. INVARIANT: scoped_ptr enforces strict unique ownership.
//    - Not copyable (prevents aliasing, double frees, and ownership ambiguity)
//    - Must be move-constructible (ownership transfer is required for routing)
// ============================================================================
TEST(TypeSafetyInvariants, scoped_ptr_enforces_strict_unique_ownership) {
    EXPECT_FALSE(std::is_copy_constructible_v<core::scoped_ptr<dummy_event>>);
    EXPECT_FALSE(std::is_copy_assignable_v<core::scoped_ptr<dummy_event>>);

    // Move semantics are mandatory for deterministic routing.
    EXPECT_TRUE(std::is_move_constructible_v<core::scoped_ptr<dummy_event>>);
}

TEST(TypeSafetyInvariants, managed_data_enforces_strict_unique_ownership) {
    using PoolType = pool<dummy_event, 16>;
    EXPECT_FALSE((std::is_copy_constructible_v<core::managed_data<dummy_event, PoolType>>));
    EXPECT_FALSE((std::is_copy_assignable_v<core::managed_data<dummy_event, PoolType>>));

    EXPECT_TRUE((std::is_move_constructible_v<core::managed_data<dummy_event, PoolType>>));
}
