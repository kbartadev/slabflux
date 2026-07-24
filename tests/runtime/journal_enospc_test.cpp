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
 * @file journal_enospc_test.cpp
 * @brief Verifies graceful behavior when the durable log runs out of space.
 */

#include <gtest/gtest.h>

#ifndef _WIN32

#include <csignal>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include "slabflux/core/wire_frame_lsn.hpp"
#include "slabflux/storage/journal_tail_validator.hpp"

using Frame = slabflux::core::wire_frame_lsn<float>;

namespace {

int child_append_past_cap(const char* path) {
    const int fd = ::open(path, O_CREAT | O_RDWR, 0644);
    if (fd < 0) {
        return 1;
    }

    rlimit file_cap{};
    file_cap.rlim_cur = sizeof(Frame);
    file_cap.rlim_max = sizeof(Frame);
    if (setrlimit(RLIMIT_FSIZE, &file_cap) != 0) {
        ::close(fd);
        return 2;
    }

    Frame frame{};
    frame.cluster_id = slabflux::storage::k_journal_magic_sf;
    frame.lsn = 1;
    frame.payload = 1.0f;

    if (::pwrite(fd, &frame, sizeof(Frame), 0) != static_cast<ssize_t>(sizeof(Frame))) {
        ::close(fd);
        return 3;
    }

    std::signal(SIGXFSZ, SIG_IGN);
    errno = 0;
    const ssize_t second = ::pwrite(fd, &frame, sizeof(Frame), sizeof(Frame));
    const int err = errno;
    ::close(fd);

    if (second >= 0) {
        return 4;
    }
    if (err != ENOSPC && err != EFBIG && err != EDQUOT) {
        return 5;
    }
    return 0;
}

}  // namespace

TEST(JournalEnospc, SecondAppendBlockedWhenFileSizeCapped) {
    const char* path = "journal_enospc_child_test.wal";
    std::remove(path);

    const pid_t child = ::fork();
    ASSERT_GE(child, 0);

    if (child == 0) {
        _exit(child_append_past_cap(path));
    }

    int status = 0;
    ASSERT_EQ(::waitpid(child, &status, 0), child);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
    std::remove(path);
}

TEST(JournalEnospc, ValidatorIgnoresTornTailAfterPartialWrite) {
    Frame frames[2]{};
    for (int i = 0; i < 2; ++i) {
        frames[i].cluster_id = slabflux::storage::k_journal_magic_sf;
        frames[i].type_id = 1;
        frames[i].lsn = static_cast<uint64_t>(i + 1);
    }

    alignas(64) uint8_t bytes[sizeof(Frame) * 2];
    size_t valid_size = sizeof(Frame) + sizeof(Frame) / 2;
    std::memcpy(bytes, frames, sizeof(Frame));
    std::memcpy(bytes + sizeof(Frame), frames + 1, sizeof(Frame) / 2);

    const size_t valid =
        slabflux::storage::valid_journal_prefix_bytes<Frame>(bytes, valid_size);
    EXPECT_EQ(valid, sizeof(Frame));
}

#endif  // !_WIN32
