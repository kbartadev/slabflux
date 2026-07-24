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
 * ============================================================================* @file pipeline_horizontal_flow.cpp
 * @brief Validation of horizontal event processing across independent subsystems.
 */

#include <gtest/gtest.h>
#include "slabflux/core.hpp"

namespace slabflux::test {

    struct execution_order {
        double price;
        uint32_t volume;
        bool is_validated = false;
        bool risk_cleared = false;
    };
    EXPECT_INHERITANCE(execution_order);

    // Subsystem 1: Structural Integrity
    struct order_validator {
        void on(execution_order& ev) {
            if (ev.volume > 0) ev.is_validated = true;
        }
    };
    EXPECT_INHERITANCE(order_validator);

    // Subsystem 2: Risk Management
    struct risk_gateway {
        void on(execution_order& ev) {
            if (ev.price < 1000000.0) ev.risk_cleared = true;
        }
    };
    EXPECT_INHERITANCE(risk_gateway);

    // Subsystem 3: Compliance & Audit
    struct audit_logger {
        int logs_recorded = 0;
        void on(execution_order& ev) {
            if (ev.is_validated && ev.risk_cleared) {
                logs_recorded++;
            }
        }
    };
    EXPECT_INHERITANCE(audit_logger);

    TEST(HorizontalPipelineTest, validates_sequential_processing_flow) {
        pool<execution_order, 10> order_pool;

        order_validator validator;
        risk_gateway risk;
        audit_logger auditor;

        // Utilize CTAD to safely bind handler lvalue references preventing state isolation
        slabflux::core::pipeline trade_pipe(validator, risk, auditor);

        auto order = order_pool.make();
        order->price = 550.5;
        order->volume = 100;

        trade_pipe.dispatch(order);

        EXPECT_TRUE(order->is_validated);
        EXPECT_TRUE(order->risk_cleared);
        EXPECT_EQ(auditor.logs_recorded, 1);
    }
}
