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
#include "slabflux/compute/physics_reactor.hpp"
#include "slabflux/core/pool.hpp"

using namespace slabflux::compute;

TEST(PhysicsReactorTest, BasicUpdate) {
    physics_reactor reactor(0.0f); // Zero viscosity

    // Create a mock pool for stimulus_event
    slabflux::core::pool<slabflux::compute::stimulus_event, 10> pool;
    auto ev = pool.make(100.0f, 1.0f); // intensity 100, confidence 1.0

    // Pass as reference
    reactor.on(*ev);

    // M_new = M + C*(S - M) - V*M
    // M_new = 0 + 1.0*(100.0 - 0) - 0*0 = 100.0
    for(size_t i=0; i<physics_reactor::STATE_SIZE; ++i) {
        EXPECT_NEAR(reactor.state_vector[i], 100.0f, 1e-5f);
    }
}

TEST(PhysicsReactorTest, Viscosity) {
    physics_reactor reactor(0.5f); // 0.5 viscosity
    slabflux::core::pool<slabflux::compute::stimulus_event, 10> pool;

    // Initial state is 0.
    // M_new = 0 + 1.0*(100.0 - 0) - 0.5*0 = 100.0
    auto ev1 = pool.make(100.0f, 1.0f);
    reactor.on(*ev1);

    // Next step:
    // M_new = 100 + 1.0*(100 - 100) - 0.5*100 = 100 + 0 - 50 = 50.0
    auto ev2 = pool.make(100.0f, 1.0f);
    reactor.on(*ev2);

    for(size_t i=0; i<physics_reactor::STATE_SIZE; ++i) {
        EXPECT_NEAR(reactor.state_vector[i], 75.0f, 1e-5f);
    }
}
