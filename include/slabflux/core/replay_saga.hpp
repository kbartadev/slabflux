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

#pragma once
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "wire_frame_lsn.hpp"
#include <immintrin.h> // For _mm_pause
#include "slabflux/compute/vector_lane_256.hpp"
#include "sf_node_ctx.hpp"

#include <iostream>

namespace slabflux::core {

    /**
     * @brief Deterministic state reconstruction engine.
     * @details Implements the Replay Saga by processing the durable log
     * to reach a bit-perfect state match with the original execution.
     */
    template<typename Payload>
    class replay_saga {
    private:
        slabflux::compute::vector_lane_256<64>& engine_;
        sf_node_ctx& context_;

    public:
        explicit replay_saga(slabflux::compute::vector_lane_256<64>& eng, sf_node_ctx& ctx)
        : engine_(eng), context_(ctx) {
        }

        /**
         * @brief Replays events from a given journal file.
         * @param path Path to the durable journal file.
         * @param target_lsn The LSN to reach (0 for full replay).
         */
        void execute(const char* path, uint64_t target_lsn = 0) {
            // HFT Optimization: Replaced O(N) read() syscalls with a single zero-copy mmap.
            // Reading 64-byte frames via sequential syscalls destroys deterministic replay speed.
            int fd = open(path, O_RDONLY | O_CLOEXEC);
            if (fd < 0) {
                std::cerr << "DEBUG: Failed to open journal " << path << std::endl;
                return;
            }

            struct stat st;
            if (fstat(fd, &st) < 0 || st.st_size == 0) {
                close(fd);
                return;
            }

            void* mapped_data = mmap(nullptr, st.st_size, PROT_READ, MAP_SHARED | MAP_POPULATE, fd, 0);
            if (mapped_data == MAP_FAILED) {
                close(fd);
                return;
            }

            const auto* frames = static_cast<const wire_frame_lsn<Payload>*>(mapped_data);
            const size_t total_frames = st.st_size / sizeof(wire_frame_lsn<Payload>);
            uint64_t count = 0;

            for (size_t i = 0; i < total_frames; ++i) {
                const auto& frame = frames[i];
                // Bit-Perfect Replay: Direct integer propagation
                engine_.propagate(static_cast<int32_t>(frame.payload), frame.lsn);

                // Advance the committed horizon
                context_.commit(frame.lsn);
                count++;

                if (target_lsn > 0 && frame.lsn >= target_lsn) break;
            }
            std::cout << "DEBUG: Replayed " << count << " frames" << std::endl;
            
            munmap(mapped_data, st.st_size);
            close(fd);
        }
    };

} // namespace slabflux::core
