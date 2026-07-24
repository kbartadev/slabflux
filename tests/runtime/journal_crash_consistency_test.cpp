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
 * @file journal_crash_consistency_test.cpp
 * @brief SIGKILL mid-write recovery with optional O_DIRECT and replay_saga parity.
 */

#include <gtest/gtest.h>

#ifndef _WIN32

#include <csignal>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <fcntl.h>

#include "slabflux/core/replay_saga.hpp"
#include "slabflux/compute/vector_lane_engine.hpp"
#include "slabflux/core/wire_frame_lsn.hpp"
#include "slabflux/storage/journal_tail_validator.hpp"

using namespace slabflux;
using Frame = core::wire_frame_lsn<float>;

namespace {

    constexpr int k_complete_frames = 48;
    constexpr size_t k_odirect_sector = 512;

    struct alignas(512) odirect_sector {
        Frame frame;
        char pad[k_odirect_sector - sizeof(Frame)];
    };

    bool write_frame(int fd, const Frame& frame, off_t offset) {
        const ssize_t n = ::pwrite(fd, &frame, sizeof(Frame), offset);
        return n == static_cast<ssize_t>(sizeof(Frame));
    }

    bool write_odirect_sector(int fd, const Frame& frame, off_t sector_index) {
        odirect_sector sector{};
        sector.frame = frame;
        const off_t offset = sector_index * static_cast<off_t>(k_odirect_sector);
        const ssize_t n = ::pwrite(fd, &sector, k_odirect_sector, offset);
        return n == static_cast<ssize_t>(k_odirect_sector);
    }

    Frame make_frame(uint64_t lsn) {
        Frame frame{};
        frame.cluster_id = storage::k_journal_magic_sf;
        frame.type_id = 1;
        frame.lsn = lsn;
        frame.payload = 42.0f + static_cast<float>(lsn);
        return frame;
    }

    void child_write_torn_journal(const char* path, bool use_odirect) {
        const int flags = O_CREAT | O_RDWR | (use_odirect ? O_DIRECT : 0);
        const int fd = ::open(path, flags, 0644);
        if (fd < 0) {
            _exit(2);
        }

        if (use_odirect) {
            for (int i = 0; i < k_complete_frames; ++i) {
                const Frame frame = make_frame(static_cast<uint64_t>(i));
                if (!write_odirect_sector(fd, frame, i)) {
                    ::close(fd);
                    _exit(4);
                }
            }
            odirect_sector torn_sector{};
            torn_sector.frame = make_frame(static_cast<uint64_t>(k_complete_frames));
            // Simulate a torn sector by corrupting the magic number,
            // which causes the validator to truncate at this sector boundary.
            torn_sector.frame.cluster_id = 0xDEADBEEF;
            write_odirect_sector(fd, torn_sector.frame, k_complete_frames);
        } else {
            for (int i = 0; i < k_complete_frames; ++i) {
                const Frame frame = make_frame(static_cast<uint64_t>(i));
                if (!write_frame(fd, frame, static_cast<off_t>(i) * static_cast<off_t>(sizeof(Frame)))) {
                    ::close(fd);
                    _exit(4);
                }
            }

            const Frame torn = make_frame(static_cast<uint64_t>(k_complete_frames));
            const ssize_t half = static_cast<ssize_t>(sizeof(Frame) / 2);
            const ssize_t n = ::pwrite(fd, &torn, half,
                                       static_cast<off_t>(k_complete_frames) * static_cast<off_t>(sizeof(Frame)));
            if (n != half) {
                ::close(fd);
                _exit(3);
            }
        }

        ::close(fd);
        while (true) {
            ::pause();
        }
    }

}  // namespace

class JournalCrashTest : public ::testing::Test {
protected:
    const char* path_ = "crash_consistency_test.wal";

    void TearDown() override { std::remove(path_); }
};

TEST_F(JournalCrashTest, SigkillMidWriteRecoversPrefix) {
    const pid_t child = ::fork();
    ASSERT_GE(child, 0);

    if (child == 0) {
        child_write_torn_journal(path_, false);
    }

    // Wait for the child to finish writing the torn journal before killing it
    size_t expected_size = k_complete_frames * sizeof(Frame) + sizeof(Frame) / 2;
    for (int i = 0; i < 50; ++i) {
        struct stat st;
        if (::stat(path_, &st) == 0 && st.st_size >= static_cast<off_t>(expected_size)) break;
        ::usleep(100'000);
    }
    ASSERT_EQ(::kill(child, SIGKILL), 0);

    int status = 0;
    ASSERT_EQ(::waitpid(child, &status, 0), child);
    EXPECT_FALSE(WIFEXITED(status));

    const size_t retained = storage::truncate_journal_tail<Frame>(path_);
    EXPECT_EQ(retained, static_cast<size_t>(k_complete_frames) * sizeof(Frame));

    std::ifstream verify(path_, std::ios::binary | std::ios::ate);
    ASSERT_TRUE(verify.good());
    EXPECT_EQ(static_cast<size_t>(verify.tellg()), retained);
}

TEST_F(JournalCrashTest, ODirectTornTailTruncatesAndReplays) {
    const int probe_fd = ::open(path_, O_CREAT | O_RDWR | O_DIRECT, 0644);
    if (probe_fd < 0) {
        GTEST_SKIP() << "O_DIRECT unavailable in this environment";
    }
    ::close(probe_fd);
    std::remove(path_);

    const pid_t child = ::fork();
    ASSERT_GE(child, 0);

    if (child == 0) {
        child_write_torn_journal(path_, true);
    }
    
    // Wait for the child to finish writing the torn journal before killing it
    size_t expected_size = (k_complete_frames + 1) * k_odirect_sector;
    for (int i = 0; i < 50; ++i) {
        struct stat st;
        if (::stat(path_, &st) == 0 && st.st_size >= static_cast<off_t>(expected_size)) break;
        ::usleep(100'000);
    }
    ASSERT_EQ(::kill(child, SIGKILL), 0);
    int status = 0;
    ASSERT_EQ(::waitpid(child, &status, 0), child);

    std::vector<uint8_t> bytes;
    {
        std::ifstream in(path_, std::ios::binary | std::ios::ate);
        ASSERT_TRUE(in.good());
        const auto file_size = static_cast<size_t>(in.tellg());
        bytes.resize(file_size);
        in.seekg(0);
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(file_size));
    }

    const size_t retained = storage::valid_journal_strided_prefix_bytes<Frame, k_odirect_sector>(
        bytes.data(), bytes.size());
    ASSERT_EQ(retained, static_cast<size_t>(k_complete_frames) * k_odirect_sector);

    {
        const int tfd = ::open(path_, O_RDWR);
        ASSERT_GE(tfd, 0);
        ASSERT_EQ(::ftruncate(tfd, static_cast<off_t>(retained)), 0);
        ::close(tfd);
    }

    // Flatten sectors into a contiguous replay log for replay_saga.
    const char* flat_path = "crash_consistency_replay.wal";
    {
        std::ofstream flat(flat_path, std::ios::binary | std::ios::trunc);
        for (int i = 0; i < k_complete_frames; ++i) {
            const auto* sector = reinterpret_cast<const odirect_sector*>(bytes.data() + i * k_odirect_sector);
            flat.write(reinterpret_cast<const char*>(&sector->frame), sizeof(Frame));
        }
    }

    compute::vector_lane_256<64> live{};
    compute::vector_lane_256<64> replay{};
    core::sf_node_ctx ctx{};

    std::memset(live.states, 0, sizeof(live.states)); // Ensure clean state for comparison
    std::memset(replay.states, 0, sizeof(replay.states));

    for (int i = 0; i < k_complete_frames; ++i) {
        live.propagate(42.0f + static_cast<float>(i), static_cast<uint64_t>(i));
    }

    core::replay_saga<float> saga(replay, ctx);
    saga.execute(flat_path);
    std::remove(flat_path);

    for (int i = 0; i < 64; ++i) {
        EXPECT_FLOAT_EQ(live.states[i], replay.states[i]);
    }
}

#endif  // !_WIN32
