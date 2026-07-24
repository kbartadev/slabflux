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
 * ============================================================================*
 * @file journal_integrity_test.cpp
 * @brief Crash-consistency: torn WAL tails and truncated replay boundaries.
 */

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#include "slabflux/core/wire_frame_lsn.hpp"
#include "slabflux/storage/journal_tail_validator.hpp"

using namespace slabflux;

namespace {

struct test_frame {
    uint32_t cluster_id = storage::k_journal_magic_sf;
    uint16_t type_id = 1;
    uint64_t lsn = 0;
    uint64_t payload = 0;
};

std::vector<uint8_t> build_valid_log(size_t frame_count) {
    std::vector<uint8_t> bytes(frame_count * sizeof(test_frame));
    for (size_t i = 0; i < frame_count; ++i) {
        test_frame frame{};
        frame.lsn = i + 1;
        frame.payload = 0xDEAD0000ULL + i;
        std::memcpy(bytes.data() + i * sizeof(test_frame), &frame, sizeof(test_frame));
    }
    return bytes;
}

}  // namespace

TEST(JournalIntegrity, ValidPrefixStopsAtTornTail) {
    auto bytes = build_valid_log(16);
    const size_t torn_bytes = sizeof(test_frame) / 2;
    bytes.resize(bytes.size() + torn_bytes, 0xAB);

    const size_t valid = storage::valid_journal_prefix_bytes<test_frame>(bytes.data(), bytes.size());
    EXPECT_EQ(valid, 16 * sizeof(test_frame));
}

TEST(JournalIntegrity, CorruptedMagicTruncatesEarlier) {
    auto bytes = build_valid_log(8);
    bytes[3 * sizeof(test_frame)] = 0x00;  // corrupt magic on frame 3

    const size_t valid = storage::valid_journal_prefix_bytes<test_frame>(bytes.data(), bytes.size());
    EXPECT_EQ(valid, 3 * sizeof(test_frame));
}

TEST(JournalIntegrity, TruncateFileRemovesTornSector) {
    const char* path = "journal_torn_test.wal";
    auto bytes = build_valid_log(4);
    bytes.insert(bytes.end(), sizeof(test_frame) / 2, 0xCD);

    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }

    std::vector<uint8_t> on_disk(bytes.size());
    {
        std::ifstream in(path, std::ios::binary);
        in.read(reinterpret_cast<char*>(on_disk.data()),
                static_cast<std::streamsize>(on_disk.size()));
    }

    const size_t valid = storage::valid_journal_prefix_bytes<test_frame>(on_disk.data(), on_disk.size());
    EXPECT_EQ(valid, 4 * sizeof(test_frame));

    on_disk.resize(valid);
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(on_disk.data()),
                  static_cast<std::streamsize>(on_disk.size()));
    }

    std::ifstream verify(path, std::ios::binary | std::ios::ate);
    ASSERT_TRUE(verify.good());
    EXPECT_EQ(static_cast<size_t>(verify.tellg()), 4 * sizeof(test_frame));

    std::remove(path);
}

TEST(JournalIntegrity, WireFrameLsnMatchesValidator) {
    core::wire_frame_lsn<uint64_t> frame{};
    frame.cluster_id = storage::k_journal_magic_sf;
    frame.type_id = 42;
    frame.lsn = 1;
    frame.payload = 12345;

    alignas(64) uint8_t bytes[sizeof(frame) * 2];
    std::memcpy(bytes, &frame, sizeof(frame));
    
    frame.lsn = 2; // LSN must be monotonic
    std::memcpy(bytes + sizeof(frame), &frame, sizeof(frame));

    EXPECT_EQ(storage::valid_journal_prefix_bytes<core::wire_frame_lsn<uint64_t>>(
                  bytes, sizeof(bytes)),
              sizeof(bytes));
}
