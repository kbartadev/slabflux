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
 * ============================================================================* @file pipeline_types.cpp
 * @brief Unit tests proving pipeline robustness across all memory wrapper types.
 */

#include <gtest/gtest.h>
#include <iostream>
#include <cstdint>
#include <cassert>

#include "slabflux/core/memory.hpp"
#include "slabflux/core/pipeline.hpp"
#include "slabflux/core/demuxer.hpp"
#include "slabflux/meta.hpp"

using namespace slabflux::core;

// --- 2. TEST EVENT ---
namespace slabflux::events {
    struct test_event {
        static constexpr uint64_t ID = 123;
        int payload;
        test_event(int p) : payload(p) {}
    };
}

// --- 3. TEST HANDLER ---
struct ptr_handler {
    int ptr_calls = 0;

    bool on(slabflux::events::test_event* ev) noexcept {
        ptr_calls++;
        ev->payload++;
        return true;
    }
};

struct ref_handler {
    int ref_calls = 0;
    bool on(slabflux::events::test_event& ev) noexcept {
        ref_calls++;
        ev.payload++;
        return true;
    }
};

// --- 4. TEST SUITE ---

TEST(PipelineDispatch, LvalueReferenceDispatch) {
    ref_handler handler;
    pipeline pipe(handler);

    slabflux::events::test_event ev(100);

    pipe.dispatch(ev);

    EXPECT_EQ(handler.ref_calls, 1);
    EXPECT_EQ(ev.payload, 101);
}

TEST(PipelineDispatch, RawPointerDispatch) {
    ptr_handler handler;
    pipeline pipe(handler);

    slabflux::events::test_event ev(200);

    pipe.dispatch(&ev);

    EXPECT_EQ(handler.ptr_calls, 1);
    EXPECT_EQ(ev.payload, 201);
}

TEST(PipelineDispatch, DemuxerTaggedPointerDispatch) {
    ptr_handler handler;
    pipeline pipe(handler);

    slabflux::events::test_event ev(400);

    tagged_pointer tp = tagged_pointer::pack(slabflux::events::test_event::ID, &ev);

    using test_bus = demuxer<slabflux::events::test_event>;
    test_bus::route(tp, pipe);

    EXPECT_EQ(handler.ptr_calls, 1);
    EXPECT_EQ(ev.payload, 401);
}
