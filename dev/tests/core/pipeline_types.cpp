/**
 * @file pipeline_types.cpp
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
        static constexpr uint16_t ID = 99;
        int payload;
        test_event(int p) : payload(p) {}
    };
}

// --- 3. TEST HANDLER ---
struct universal_handler {
    int ptr_calls = 0;
    int ref_calls = 0;

    bool on(slabflux::events::test_event& ev) noexcept {
        ref_calls++;
        ev.payload++;
        return true;
    }

    bool on(slabflux::events::test_event& ev) noexcept {
        ref_calls++;
        ev->payload++;
        return true;
    }

    void reset() {
        ptr_calls = 0;
        ref_calls = 0;
    }
};

// --- 4. TEST SUITE ---

TEST(PipelineDispatch, LvalueReferenceDispatch) {
    using namespace slabflux::events;

    universal_handler handler;
    pipeline pipe(handler);

    handler.reset();
    test_event ev(100);

    pipe.dispatch(ev);

    EXPECT_EQ(handler.ref_calls, 1);
    EXPECT_EQ(handler.ptr_calls, 0);
    EXPECT_EQ(ev.payload, 101);
}

TEST(PipelineDispatch, DemuxerTaggedPointerDispatch) {
    using namespace slabflux::events;

    universal_handler handler;
    pipeline pipe(handler);

    handler.reset();
    test_event ev(400);

    tagged_pointer tp = tagged_pointer::pack(test_event::ID, &ev);

    using test_bus = demuxer<test_event>;
    test_bus::route(tp, pipe);

    EXPECT_EQ(handler.ptr_calls, 0);
    EXPECT_EQ(handler.ref_calls, 1);
    EXPECT_EQ(ev.payload, 401);
}
