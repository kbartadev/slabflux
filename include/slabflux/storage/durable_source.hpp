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
 * @file durable_source.hpp

 */

#pragma once
#include <thread> // Required for std::this_thread::yield()

#include "../core.hpp"
#include <fcntl.h>
#include <system_error>
#include <iostream>
#include <thread>

#if defined(_WIN32)
#include <io.h>
#define OS_READ _read
#define OS_OPEN _open
#define OS_CLOSE _close
#define OS_O_RDONLY _O_RDONLY
#define OS_O_BINARY _O_BINARY
#else
#include <unistd.h>
#define OS_READ ::read
#define OS_OPEN ::open
#define OS_CLOSE ::close
#define OS_O_RDONLY O_RDONLY
#define OS_O_BINARY 0
#endif

namespace slabflux::storage {

    /**
     * @brief Constexpr Alignment Verification Layer.
     * @details Statically validates the relationship between the event type 
     * and the persistent medium's physical stride. Eliminates standard 
     * pread offset by synthesizing the traversal pattern.
     */
    template <typename T>
    struct journal_source_synthesis {
        static constexpr std::size_t stride = (sizeof(T) + 63) & ~std::size_t(63);
    };

    // ============================================================
    // 11th INVARIANT: DURABLE MESSAGE BROKER LAYER (REPLAY)
    // Blocking reader running on a dedicated I/O thread that refills the Core.
    // ============================================================

    template <typename DomainType, typename TargetConduit, core::POD Event>
    class durable_source {
        static_assert(journal_source_synthesis<Event>::stride >= sizeof(Event), "Structural Breach: Stride underflow");

        DomainType& domain_;
        TargetConduit& conduit_;
        int fd_{-1};
        bool is_running_{false};
        static constexpr size_t STRIDE = journal_source_synthesis<Event>::stride;

    public:
        durable_source(DomainType& domain, TargetConduit& conduit, const char* filepath)
            : domain_(domain), conduit_(conduit) {
            fd_ = OS_OPEN(filepath, OS_O_RDONLY | OS_O_BINARY);
            if (fd_ == -1) {
                std::cerr << "[SLABFLUX Fatal] Failed to open durable log for replay: " << filepath << "\n";
                // In production we might not terminate here; we could notify the Orchestrator instead.
            }
        }

        ~durable_source() {
            if (fd_ != -1) {
                OS_CLOSE(fd_);
            }
        }

        // The Replay Loop: runs on a dedicated I/O thread
        void run_replay() noexcept {
            if (fd_ == -1) return;
            is_running_ = true;

            uint64_t current_offset = 0;
            while (is_running_) {
                // 1. Gate Priming: request pre-allocated O(1) memory from the Core.
                auto ev = domain_.template make_uninitialized<Event>();
                
                if (!ev) {
                    // The I/O thread yields so we don't overload the system.
                    std::this_thread::yield();
                    continue;
                }

                // 2. Physical Pulse: Synchronized read utilizing the synthesized stride.
                // Replaces standard offset iteration with bit-perfect block alignment.
                char* raw_buffer = reinterpret_cast<char*>(ev.get());
#if !defined(_WIN32)
                // Position-aware read prevents cursor-drift
                ssize_t bytes_read = ::pread(fd_, raw_buffer, sizeof(Event), current_offset);
#else
                int bytes_read = OS_READ(fd_, raw_buffer, sizeof(Event));
#endif

                if (bytes_read == static_cast<ssize_t>(sizeof(Event))) {
                    // 3. O(1) Push into the conduit. From here the Core takes over again.
                    while (!conduit_.push(std::move(ev)) && is_running_) {
                        // If the conduit is full, wait (Physical Backpressure from the network side).
                        std::this_thread::yield();
                    }
                    current_offset += STRIDE;
                } else if (bytes_read == 0) {
                    // End of file (EOF). Replay finished.
                    std::cout << "[SLABFLUX Replay] End of durable log reached.\n";
                    break;
                } else {
                    // Corrupted file or partial read (a more robust implementation
                    // would handle partial reads like networked_conduit does).
                    std::cerr << "[SLABFLUX Error] Corrupted read during replay.\n";
                    break;
                }
            }
            is_running_ = false;
        }

        void stop() noexcept {
            is_running_ = false;
        }
    };

} // namespace slabflux::storage
