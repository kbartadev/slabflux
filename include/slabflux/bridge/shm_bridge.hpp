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
 * @file shm_bridge.hpp
 * @brief Cross-Process Shared Memory Bridge Engine.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <new>

#ifndef _WIN32
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#else
#include <windows.h>
#endif

#include <stdexcept>
#include "slabflux/core/memory.hpp"
#include "slabflux/core/spsc_ring_conduit.hpp"
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::io {
    enum class ipc_role : uint8_t { creator, joiner };
}

namespace slabflux::bridge {

    /**
     * @brief Zero-Allocation Cross-Process Shared Memory Interconnect.
     * @details Enforces page locking, TLB optimization, and eliminates pointer indirection.
     * @tparam EventType The underlying network or execution event type passing through the conduit.
     * @tparam Capacity Total slot capacity allocated to the lock-free data conduit. Must be a power-of-two.
     */
    template <typename EventType, size_t Capacity>
    class alignas(64) shm_bridge {
    private:
        static constexpr std::size_t PAGE_SIZE = 4096;

        std::size_t total_size_{0};
        void* mapped_mem_{nullptr};
        io::ipc_role    role_;
        uint8_t     error_state_{0};

        // Fixed character array eliminates heap tracking overhead from std::string
        char        shm_name_[256]{0};

        #ifndef _WIN32
        int shm_fd_{-1};
        #else
        HANDLE hMapFile_{nullptr};
        #endif

    public:
        explicit shm_bridge(const std::string& name, io::ipc_role role)
        : role_(role)
        {
            if (name.empty()) [[unlikely]]
                throw std::invalid_argument("SHM Bridge: Name prefix cannot be empty.");

            std::strncpy(shm_name_, name.c_str(), sizeof(shm_name_) - 1);
            total_size_ = sizeof(slabflux::core::spsc_ring_conduit<EventType, Capacity>);
            total_size_ = (total_size_ + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1); // Exact hardware page rounding

            #ifndef _WIN32
            if (role_ == io::ipc_role::creator) {
                ::shm_unlink(shm_name_);
                shm_fd_ = ::shm_open(shm_name_, O_CREAT | O_RDWR | O_EXCL, 0666);
            } else {
                shm_fd_ = ::shm_open(shm_name_, O_RDWR, 0666);
            }

            if (shm_fd_ < 0) [[unlikely]]
                throw std::runtime_error("SHM Bridge: shm_open failed for " + name);

            if (role_ == io::ipc_role::creator) {
                if (::ftruncate(shm_fd_, static_cast<off_t>(total_size_)) != 0) [[unlikely]] {
                    ::close(shm_fd_);
                    throw std::runtime_error("SHM Bridge: ftruncate failed for " + name);
                }
            }

            // High-Performance Mapping Flags: MAP_SHARED + MAP_POPULATE to trigger immediate hardware page faults
            // and MAP_LOCKED to pin the pages into physical RAM, preventing kernel swap transitions.
            mapped_mem_ = ::mmap(nullptr, total_size_, PROT_READ | PROT_WRITE,
                                 MAP_SHARED | MAP_POPULATE | MAP_LOCKED, shm_fd_, 0);
            if (mapped_mem_ == MAP_FAILED) [[unlikely]] {
                // Fallback to unlocked mapping if process privileges restrict mlock resources
                mapped_mem_ = ::mmap(nullptr, total_size_, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, shm_fd_, 0);
            }

            if (mapped_mem_ == MAP_FAILED || mapped_mem_ == nullptr) [[unlikely]] {
                ::close(shm_fd_);
                throw std::runtime_error("SHM Bridge: Physical mapping failed. Check ulimit -l and /dev/shm permissions.");
            }
            #else
            if (role_ == io::ipc_role::creator) {
                hMapFile_ = ::CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, static_cast<DWORD>(total_size_), shm_name_);
            } else {
                hMapFile_ = ::OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, shm_name_);
            }

            if (!hMapFile_) [[unlikely]] {
                error_state_ = 5;
                return;
            }

            mapped_mem_ = ::MapViewOfFile(hMapFile_, FILE_MAP_ALL_ACCESS, 0, 0, total_size_);
            if (!mapped_mem_) [[unlikely]] {
                ::CloseHandle(hMapFile_);
                error_state_ = 6;
                return;
            }
            #endif

            if (role_ == io::ipc_role::creator) {
                // Atomically initialize the memory segment via system placement new contract
                new (mapped_mem_) slabflux::core::spsc_ring_conduit<EventType, Capacity>();
            }
        }

        ~shm_bridge() noexcept {
            if (mapped_mem_ && mapped_mem_ != MAP_FAILED) {
                if (role_ == io::ipc_role::creator) {
                    // Explicitly execute destructor sequence for shared control atomics
                    wire().~spsc_ring_conduit();
                }
                #ifndef _WIN32
                ::munmap(mapped_mem_, total_size_);
                if (shm_fd_ != -1) {
                    ::close(shm_fd_);
                }
                if (role_ == io::ipc_role::creator) {
                    ::shm_unlink(shm_name_);
                }
                #else
                ::UnmapViewOfFile(mapped_mem_);
                if (hMapFile_) {
                    ::CloseHandle(hMapFile_);
                }
                #endif
            }
        }

        // Zero-indirection wire access
        shm_bridge(const shm_bridge&) = delete;
        shm_bridge& operator=(const shm_bridge&) = delete;

        /** @brief Returns a direct runtime error state signature without executing hot path branches. */
        [[nodiscard]] SLAB_FORCE_INLINE uint8_t get_error_state() const noexcept {
            return error_state_;
        }

        /**
         * @brief Fetches a direct, single-cycle zero-indirection reference to the shared conduit layer.
         * @details Completely eliminates member pointer redirection by overlaying the structural casting layout directly.
         */
        [[nodiscard]] SLAB_FORCE_INLINE slabflux::core::spsc_ring_conduit<EventType, Capacity>& wire() noexcept {
            return *reinterpret_cast<slabflux::core::spsc_ring_conduit<EventType, Capacity>*>(mapped_mem_);
        }
    };
} // namespace slabflux::io
