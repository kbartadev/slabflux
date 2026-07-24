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
 * @brief Validates a sequential WAL prefix and returns the byte length safe to replay.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#if not defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace slabflux::storage {

    static constexpr uint16_t k_journal_magic_sf = 0x4653;  // 'SF'

    /**
     * @brief Scans a durable log for contiguous valid frames.
     * @return Byte offset after the last valid frame (truncate torn tail here).
     * @details Stops at the first magic mismatch or incomplete trailing record.
     */
    template<typename Frame>
    [[nodiscard]] size_t valid_journal_prefix_bytes(const uint8_t* data, size_t file_size) noexcept {
        static_assert(sizeof(Frame) > 4, "Frame must carry magic at offset 0");
        const size_t frame_size = sizeof(Frame);
        size_t offset = 0;

        while (offset + frame_size <= file_size) {
            uint16_t magic = 0;
            std::memcpy(&magic, data + offset, sizeof(magic));
            if (magic != k_journal_magic_sf) {
                break;
            }
            offset += frame_size;
        }

        return offset;
    }

    /**
     * @brief Validates frames stored at a fixed stride (e.g. O_DIRECT 512-byte sectors).
     */
    template<typename Frame, size_t Stride>
    [[nodiscard]] size_t valid_journal_strided_prefix_bytes(const uint8_t* data,
                                                            size_t file_size) noexcept {
        static_assert(Stride >= sizeof(Frame), "Stride must fit a frame");
        size_t offset = 0;

        while (offset + sizeof(Frame) <= file_size) {
            uint16_t magic = 0;
            std::memcpy(&magic, data + offset, sizeof(magic));
            if (magic != k_journal_magic_sf) {
                break;
            }
            offset += Stride;
        }

        return offset;
    }

#if not defined(_WIN32)
    /**
     * @brief Truncates a WAL file to the last contiguous valid frame boundary.
     * @return Bytes retained after truncation, or 0 on I/O failure.
     */
    template<typename Frame>
    [[nodiscard]] size_t truncate_journal_tail(const char* path) noexcept {
        const int fd = ::open(path, O_RDWR);
        if (fd < 0) {
            return 0;
        }

        const off_t file_size = ::lseek(fd, 0, SEEK_END);
        if (file_size <= 0) {
            ::close(fd);
            return 0;
        }

        std::vector<uint8_t> buffer(static_cast<size_t>(file_size));
        if (::lseek(fd, 0, SEEK_SET) < 0) {
            ::close(fd);
            return 0;
        }

        const ssize_t bytes_read = ::read(fd, buffer.data(), buffer.size());
        if (bytes_read <= 0) {
            ::close(fd);
            return 0;
        }

        const size_t valid = valid_journal_prefix_bytes<Frame>(buffer.data(),
                                                               static_cast<size_t>(bytes_read));
        if (::ftruncate(fd, static_cast<off_t>(valid)) != 0) {
            ::close(fd);
            return 0;
        }

        ::close(fd);
        return valid;
    }
#endif

}  // namespace slabflux::storage
