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
#include "slabflux/sys/state_migrator.hpp"
#include "slabflux/sys/schema.hpp"

using namespace slabflux::sys;

struct state_v1 {
    uint32_t id;
    double price;
};

struct state_v2 {
    double price; // Reordered
    uint32_t id;
    uint64_t new_field; // Added
};

/**
 * @brief Verifies that state can be migrated across reordered layouts.
 */
TEST(StateManagement, MigratorHandlesLayoutChanges) {
    state_v1 v1{42, 100.5};
    state_v2 v2{0.0, 0, 999};

    schema s1;
    s1.register_field("id", field_type::u32, offsetof(state_v1, id), sizeof(uint32_t));
    s1.register_field("price", field_type::f64, offsetof(state_v1, price), sizeof(double));

    schema s2;
    s2.register_field("price", field_type::f64, offsetof(state_v2, price), sizeof(double));
    s2.register_field("id", field_type::u32, offsetof(state_v2, id), sizeof(uint32_t));
    s2.register_field("new_field", field_type::u64, offsetof(state_v2, new_field), sizeof(uint64_t));

    state_migrator::migrate(&v1, &v2, s1, s2);

    EXPECT_EQ(v2.id, 42);
    EXPECT_EQ(v2.price, 100.5);
    EXPECT_EQ(v2.new_field, 999); // Unchanged because field was not in v1
}