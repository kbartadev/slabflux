#include <gtest/gtest.h>
#include <string>
#include <memory>
#include "slabflux/core.hpp"
#include "slabflux/core/string_chunk.hpp"

/**
 * @struct MockChunkPool
 * @brief Improved, safe Mock Pool for testing that does not corrupt the stack.
 */
template<uint32_t Slabs = 64>
struct MockChunkPool {
    slabflux::core::string_chunk storage[Slabs];
    uint32_t free_stack[Slabs];
    uint32_t free_top{ 0 };
    uint32_t allocated{ 0 };

    slabflux::core::string_chunk* make() {
        uint32_t idx;
        if (free_top > 0) {
            idx = free_stack[--free_top];
        }
        else {
            if (allocated >= Slabs) return nullptr;
            idx = allocated++;
        }
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

    /** @brief Sovereign Deleter implementation for Mock Pool. */
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

TEST(StringIntegration, VerifiesConduitSPSCTransfer) {
    auto blob_pool = std::make_unique<MockChunkPool<64>>();
    pool<trade_log_event, 16> event_pool;
    conduit<trade_log_event*, 32> ring_buffer;

    string_service<MockChunkPool<64>> str_svc(*blob_pool);

    auto ev = event_pool.make();
    ASSERT_NE(ev, nullptr);

    ev->timestamp = 123456789;
    str_svc.assign(ev->payload, "Critical trade execution logged via SPSC.");

    trade_log_event* raw_ptr = ev.release();
    bool pushed = ring_buffer.try_push(raw_ptr);
    ASSERT_TRUE(pushed);

    auto received = ring_buffer.try_pop(event_pool);
    ASSERT_TRUE(received);
    EXPECT_EQ(received->timestamp, 123456789);

    std::string msg = str_svc.extract_to_std_string(received->payload);
    EXPECT_EQ(msg, "Critical trade execution logged via SPSC.");

    str_svc.clear(received->payload);
}

struct order_event {
    uint32_t order_id;
    fragmented_string instructions;
};

TEST(StringIntegration, ValidatesSyntaxAndRelocatability) {
    // 1. Setup Physical Infrastructure
    MockChunkPool<64> chunk_pool;
    pool<order_event, 10> event_pool;
    string_service<MockChunkPool<64>> str_svc(chunk_pool);

    // 2. Allocation (O(1) Slab)
    auto ev = event_pool.make();
    ev->order_id = 5005;

    {
        // 3. Supplemental Automation: Wrap the POD in an Accessor
        auto active_str = str_svc.wrap(ev->instructions);

        // 4. REQUIREMENT: operator= (std::string-like assignment)
        active_str = "Execute at limit 150.25; route via dark pool Alpha.";

        // 5. REQUIREMENT: Implicit Read Out (Conversion)
        std::string result = active_str;
        EXPECT_EQ(result, "Execute at limit 150.25; route via dark pool Alpha.");

        // Cleanup chunks before the accessor loses service context
        active_str.clear();
    }

    // 6. Verification of POD Integrity
    EXPECT_EQ(ev->instructions.total_length, 0);
    EXPECT_EQ(ev->instructions.head_idx, string_chunk::END_OF_CHAIN);
}
