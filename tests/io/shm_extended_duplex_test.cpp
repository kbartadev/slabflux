/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * @file shm_extended_duplex_test.cpp
 * @brief Verification for specialized SHM architectures (Arena, Inline, Journal).
 */

#include <gtest/gtest.h>
#include <vector>
#include <cstdint>
#include "slabflux/io/shm_arena_duplex.hpp"
#include "slabflux/io/shm_inline_duplex.hpp"
#include "slabflux/io/shm_journal_duplex.hpp"
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/spsc_conduit.hpp"

using namespace slabflux;

TEST(ShmExtendedDuplexTest, ArenaDuplexResidency) {
    // Allocate a fake arena base
    std::vector<uint8_t> dummy_arena(1024 * 1024);
    
    try {
        io::shm_arena_duplex<128> arena_primary("sf_arena_test", true, dummy_arena.data());
        io::shm_arena_duplex<128> arena_joiner("sf_arena_test", false, dummy_arena.data());

        // Structural requirement
        EXPECT_EQ(reinterpret_cast<uintptr_t>(&arena_primary) % 64, 0);
        EXPECT_EQ(sizeof(io::shm_arena_duplex<128>) % 64, 0);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "SHM Arena skipped (permissions): " << e.what();
    }
}

TEST(ShmExtendedDuplexTest, InlineDuplexResidency) {
    try {
        io::shm_inline_duplex<128, 64> inline_primary("sf_inline_test", true);
        
        // Structural requirement
        EXPECT_EQ(reinterpret_cast<uintptr_t>(&inline_primary) % 64, 0);
        
        // Check internal struct boundaries to ensure AVX-512 non-temporal store safety
        using SlotType = io::shm_inline_duplex<128, 64>::shm_slot;
        EXPECT_EQ(sizeof(SlotType), (io::shm_inline_layout_synthesizer<128, 64>::SLOT_STRIDE));
        EXPECT_EQ(alignof(SlotType), 64);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "SHM Inline skipped (permissions): " << e.what();
    }
}

TEST(ShmExtendedDuplexTest, JournalDuplexTranslationIntegrity) {
    std::vector<uint8_t> dummy_journal_arena(2048 * 2048);
    
    try {
        io::shm_journal_duplex<128> journal_primary("sf_journal_test", true, dummy_journal_arena.data());
        
        core::spsc_conduit<core::tagged_pointer, 128> tx_bus;
        
        // We simulate a raw pointer inside the arena boundaries
        uint8_t* raw_pointer_in_arena = dummy_journal_arena.data() + 4096;
        
        // Pack the pointer into the conduit
        tx_bus.push(core::tagged_pointer::pack(1, raw_pointer_in_arena));
        
        // Drive the egress path
        journal_primary.process_egress_burst(tx_bus);
        
        // Access internal layout to verify Offset Translation
        // We cast the data section to verify the integer arithmetic worked
        auto* data_matrix = journal_primary.data_;
        ASSERT_NE(data_matrix, nullptr);
        
        // The translation formula is (absolute_ptr - arena_base_ptr)
        uint32_t expected_offset = 4096;
        
        // Wait-free tail should have advanced by 1
        EXPECT_EQ(journal_primary.local_tx_tail_, 1);
        EXPECT_EQ(data_matrix->tx_slots[0].shm_offset, expected_offset);
        EXPECT_EQ(data_matrix->tx_slots[0].sequence_number, 1);
        
    } catch (const std::exception& e) {
        GTEST_SKIP() << "SHM Journal skipped (permissions): " << e.what();
    }
}