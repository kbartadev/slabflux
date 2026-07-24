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

#include <iostream>
#include <system_error>
#include <thread>

#include "../core.hpp"

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

// ============================================================
// 11. INVARIANT: DURABLE MESSAGE BROKER LAYER (REPLAY)
// Blocking reader running on a dedicated I/O thread that refills the Core.
// ============================================================

template <typename DomainType, typename TargetConduit, typename Event>
class durable_source {
    DomainType& domain_;
    TargetConduit& conduit_;
    int fd_{-1};
    bool is_running_{false};

   public:
    durable_source(DomainType& domain, TargetConduit& conduit, const char* filepath)
        : domain_(domain), conduit_(conduit) {
        fd_ = OS_OPEN(filepath, OS_O_RDONLY | OS_O_BINARY);
        if (fd_ == -1) {
            std::cerr << "[SLABFLUX Fatal] Failed to open durable log for replay: " << filepath
                      << "\n";
            // In production we might not terminate here; we may simply notify the Orchestrator.
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

        const size_t event_size = sizeof(Event);

        while (is_running_) {
            // 1. Gate priming: request preallocated O(1) empty memory from the Core.
            auto ev = domain_.template make();

            if (!ev) {
                // Backpressure from the Core: the pool is full, the Core cannot keep up.
                // The I/O thread yields so we don’t overload the system.
                std::this_thread::yield();
                continue;
            }

            // 2. Blocking OS read into raw memory (zero deserialization)
            char* raw_buffer = reinterpret_cast<char*>(ev.get());
            int bytes_read = OS_READ(fd_, raw_buffer, event_size);

            if (bytes_read == event_size) {
                // GENERIC O_DIRECT FULL-BLOCK PADDING GUARD
                // We only skip the record if EVERY byte inside the chunk is absolutely zero.
                const auto* u64_ptr = reinterpret_cast<const uint64_t*>(raw_buffer);
                const size_t words = event_size / 8;
                bool is_all_zero = true;

                for (size_t w = 0; w < words; ++w) {
                    if (u64_ptr[w] != 0) {
                        is_all_zero = false;
                        break;
                    }
                }

                // If the entire structure size is filled with 0s, it is filesystem padding. Skip it.
                if (is_all_zero) [[unlikely]] {
                    continue;
                }

                // 3. O(1) push into the conduit. From here the Core takes over.
                while (is_running_) {
                    if (conduit_.try_push(ev.get())) {
                        ev.release();
                        break;
                    }
                    // If the conduit is full, wait (physical backpressure from the network side).
                    std::this_thread::yield();
                }
            } else if (bytes_read == 0) {
                // End of file (EOF). Replay completed.
                std::cout << "[SLABFLUX Replay] End of durable log reached.\n";
                break;
            } else {
                // Corrupted file or partial read (a more robust implementation
                // would handle partial reads like network_conduit does).
                std::cerr << "[SLABFLUX Error] Corrupted read during replay.\n";
                break;
            }
        }
        is_running_ = false;
    }

    void stop() noexcept { is_running_ = false; }
};

}  // namespace slabflux::storage
