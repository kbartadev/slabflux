#include <gtest/gtest.h>
#include "slabflux/bridge/conduit_primitives.hpp"

using namespace slabflux::conduit;

TEST(ConduitPrimitivesTest, IronRingBufferMemoryInvariants) {
    // Constraint: ContextSize must be power of 2 for bitmasking
    constexpr size_t Size = 1024;
    iron_ring_buffer<4, Size> buffer;

    // Verify 64-byte alignment for SIMD throughput
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&buffer.memory_ring) % 64, 0);

    // Validate bitmask wrap-around (bit-level stability)
    for(size_t i = 0; i < Size + 1; ++i) {
        buffer.process_signal({1.0f, 2.0f, 3.0f, 4.0f});
    }
    EXPECT_EQ(buffer.head_index, 1);
}
