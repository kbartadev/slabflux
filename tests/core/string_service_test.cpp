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
#include <string>
#include <memory>
#include "slabflux/core.hpp"
#include "slabflux/core/string_chunk.hpp"

/**
 * @struct MockChunkPool
 * @brief Improved, safe Mock Pool for testing that does not corrupt the stack.
 */
template<uint32_t Slabs> // Use Slabs as template parameter for capacity
struct MockChunkPool {
    slabflux::core::string_chunk storage[Slabs];
    uint32_t free_stack[Slabs];
    uint32_t free_top{ 0 };
    uint32_t allocated{ 0 };

    slabflux::core::string_chunk* make() {
        uint32_t idx;
        if (free_top > 0) {
            idx = free_stack[--free_top];
        } else {
            if (allocated >= Slabs) return nullptr;
            idx = allocated++;
        }
        // Use placement new to construct the string_chunk object
        return new (&storage[idx]) slabflux::core::string_chunk();
    }

    /** @brief Amortized Batch Allocation for string_service compliance. */
    size_t make_batch(slabflux::core::string_chunk** out_ptrs, size_t count) noexcept {
        size_t actual = 0;
        while (actual < count) {
            auto* p = make();
            if (!p) break;
            out_ptrs[actual++] = p;
        }
        return actual;
    }

    /** @brief Collective Reclamation for string_service compliance. */
    void release_batch(slabflux::core::string_chunk** ptrs, size_t count) noexcept {
        for (size_t i = 0; i < count; ++i) {
            free(ptrs[i]);
        }
    }

    /** @brief Deleter implementation for Mock Pool. */
    static void deleter_fn(void* ctx, void* ptr) noexcept {
        if (ptr) static_cast<MockChunkPool*>(ctx)->free(static_cast<slabflux::core::string_chunk*>(ptr));
    }

    void free(slabflux::core::string_chunk* p) noexcept {
        if (!p) return;
        uint32_t idx = get_index(p);
        free_stack[free_top++] = idx;
    }

    uint32_t get_index(slabflux::core::string_chunk* p) const noexcept {
        return static_cast<uint32_t>(p - storage);
    }

    slabflux::core::string_chunk* get_by_index(uint32_t idx) noexcept {
        return (idx == slabflux::core::string_chunk::END_OF_CHAIN) ? nullptr : &storage[idx];
    }

    uint32_t get_free_count() const noexcept {
        return Slabs - allocated + free_top;
    }
};

#include "slabflux/core/string_service.hpp"
using namespace slabflux;
using namespace slabflux::core;

struct trade_log_event {
    uint64_t timestamp;
    fragmented_string payload;
};

TEST(StringService, VerifiesConduitSPSCTransfer) {
    // 1. Setup: create pool, event pool, and SPSC conduit
    auto blob_pool = std::make_unique<MockChunkPool<64>>();
    pool<trade_log_event, 16> event_pool;
    spsc_conduit<trade_log_event*, 32> ring_buffer;

    string_service<MockChunkPool<64>> str_svc(*blob_pool);

    // 2. Allocate event
    auto ev = event_pool.make();
    ASSERT_TRUE(ev);

    // 3. Write timestamp and payload
    ev->timestamp = 123456789;
    str_svc.assign(ev->payload, "Critical trade execution logged via SPSC.");

    // 4. Push into SPSC ring buffer
    bool pushed = ring_buffer.try_push(ev.release());
    ASSERT_TRUE(pushed);

    // 5. Pop on consumer side
    auto received = ring_buffer.try_pop(event_pool);
    ASSERT_TRUE(received);
    EXPECT_EQ(received->timestamp, 123456789);

    // 6. Extract payload and verify
    std::string msg = str_svc.extract_to_std_string(received->payload);
    EXPECT_EQ(msg, "Critical trade execution logged via SPSC.");

    // 7. Cleanup
    str_svc.clear(received->payload);
}

struct order_event {
    uint32_t order_id;
    fragmented_string instructions;
};

TEST(StringService, ValidatesSyntaxAndRelocatability) {
    // 1. Setup physical infrastructure
    MockChunkPool<64> chunk_pool;
    pool<order_event, 10> event_pool;
    string_service<MockChunkPool<64>> str_svc(chunk_pool);

    // 2. Allocation (O(1) slab)
    auto ev = event_pool.make();
    ev->order_id = 5005;

    {
        // 3. Supplemental automation: wrap the POD in an accessor
        auto active_str = str_svc.wrap(ev->instructions);

        // 4. REQUIREMENT: operator= (std::string-like assignment)
        active_str = "Execute at limit 150.25; route via dark pool Alpha.";

        // 5. REQUIREMENT: implicit read-out (conversion)
        std::string result = active_str;
        EXPECT_EQ(result, "Execute at limit 150.25; route via dark pool Alpha.");

        // Cleanup chunks before the accessor loses service context
        active_str.clear();
    }

    // 6. Verification of POD integrity
    EXPECT_EQ(ev->instructions.total_length, 0);
    EXPECT_EQ(ev->instructions.head_idx, string_chunk::END_OF_CHAIN);
}
