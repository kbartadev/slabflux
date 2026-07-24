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
 * ============================================================================* @brief SLABFLUX - Hot Path Integration Audit
 */

#include <gtest/gtest.h>
#include "slabflux/core/hot_path_alignment.hpp"
#include <immintrin.h> // For _mm_pause

using namespace slabflux::core;

struct mock_engine {
    bool called = false;
    void on_fast_path(const char*, std::string_view) { called = true; }
};

struct mock_ingress {
    void* next_ptr = nullptr;
    void* poll_next() { return next_ptr; }
    void prefetch_next() {}
};

struct mock_journal {
    bool persisted = false;
    void persist_event(void*, size_t, int) { persisted = true; }
};

TEST(HotPathTest, CriticalStepExecutionFlow) {
    mock_engine engine;
    mock_ingress ingress;
    mock_journal journal;
    
    char frame_data[64];
    ingress.next_ptr = frame_data;
    
    // Execute 1 cycle
    critical_path_step(engine, ingress, journal);
    
    EXPECT_TRUE(journal.persisted);
    EXPECT_TRUE(engine.called);
}

TEST(HotPathTest, IdleCyclePreservation) {
    mock_engine engine;
    mock_ingress ingress;
    mock_journal journal;
    
    ingress.next_ptr = nullptr; // No data
    critical_path_step(engine, ingress, journal);
    
    EXPECT_FALSE(engine.called);
}