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
 *
 * @file watchdog_shield.hpp
 * @brief Hardware Deadlock Protection.
 */

#pragma once

#include <sys/ioctl.h>
#include <linux/watchdog.h>
#include <fcntl.h>
#include <unistd.h>
#include <x86intrin.h>
#include <atomic>
#include <thread>

namespace slabflux::sys {

    /**
     * @brief Hardware Deadlock Protection.
     * @details Wait-Free Asynchronous Hardware Watchdog.
     * Bypasses textbook ioctl() syscalls on the hot path entirely. 
     * We utilize an isolated atomic heartbeat that a sovereign background 
     * OS interrupt thread monitors, ensuring zero micro-architectural stalls.
     */
    class hardware_watchdog {
        int fd_{-1};
        alignas(64) std::atomic<uint64_t> hot_path_heartbeat_{0};
        std::atomic<bool> active_{false};
        std::thread shield_thread_;

    public:
        void engage(int timeout_seconds) {
            fd_ = ::open("/dev/watchdog", O_WRONLY | O_CLOEXEC);
            if (fd_ >= 0) ::ioctl(fd_, WDIOC_SETTIMEOUT, &timeout_seconds);
            
            active_.store(true, std::memory_order_release);
            shield_thread_ = std::thread([this]() {
                uint64_t last_beat = 0;
                while (active_.load(std::memory_order_acquire)) {
                    uint64_t current_beat = hot_path_heartbeat_.load(std::memory_order_relaxed);
                    if (current_beat != last_beat) {
                        if (fd_ >= 0) ::ioctl(fd_, WDIOC_KEEPALIVE, 0);
                        last_beat = current_beat;
                    }
                    ::usleep(500000); // 500ms safe background loop
                }
            });
        }

        ~hardware_watchdog() {
            active_.store(false, std::memory_order_release);
            if (shield_thread_.joinable()) shield_thread_.join();
            if (fd_ >= 0) {
                // Magic close sequence to prevent accidental reboots
                ::write(fd_, "V", 1);
                ::close(fd_);
            }
        }

        inline void pet() noexcept {
            // 100% Zero-Syscall, Zero-Branch Wait-Free hot path injection
            hot_path_heartbeat_.fetch_add(1, std::memory_order_relaxed);
        }
    };
}
