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

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <string_view>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/memory.hpp"

namespace slabflux::io {

    /**
     * @brief Cross-Process Consteval Mapping Guard.
     * @details Statically derives the physical envelopes for relocatable 
     * SHM segments, ensuring bit-perfect page alignment and isolation.
     */
    template <std::size_t Capacity>
    struct shm_arena_layout_synthesis {
        static constexpr std::size_t PAGE_SIZE = 4096;
        static constexpr std::size_t CTRL_SIZE = (64 * 4 + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        static constexpr std::size_t SLOT_SIZE = 64; // sizeof(zero_copy_descriptor)
        static constexpr std::size_t MATRIX_SIZE = (SLOT_SIZE * Capacity * 2 + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    };

    struct alignas(64) zero_copy_descriptor {
        uint32_t shm_offset;
        uint16_t payload_length;
        uint16_t connection_id;
        uint64_t sequence_number;
        char padding[48];
    };

    template <std::size_t Capacity = 4096>
    class alignas(64) shm_arena_duplex {
        static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be an exact power of 2");
        using Synthesis = shm_arena_layout_synthesis<Capacity>;
        static constexpr std::size_t MASK = Capacity - 1;

        struct alignas(4096) shm_control_block {
            alignas(64) std::atomic<std::size_t> physical_tx_gate{0}; // Published Tail
            alignas(64) std::atomic<std::size_t> physical_tx_ack{0};  // Published Head
            alignas(64) std::atomic<std::size_t> physical_rx_gate{0}; // Published Tail
            alignas(64) std::atomic<std::size_t> physical_rx_ack{0};  // Published Head
        };

        struct alignas(4096) shm_data_matrix {
            zero_copy_descriptor tx_slots[Capacity];
            alignas(4096) zero_copy_descriptor rx_slots[Capacity];
        };

        shm_control_block* ctrl_{nullptr};
        shm_data_matrix* data_{nullptr};
        uint8_t* arena_base_ptr_{nullptr};
        int ctrl_fd_{-1}, data_fd_{-1};
        bool is_primary_{false};
        char c_name_[128]{0}, d_name_[128]{0};

        // --------------------------------------------------------------------
        // CORE-LOCAL REGISTERS: Grouped to eliminate internal RFO stalls
        // --------------------------------------------------------------------
        alignas(64) std::size_t local_tx_cursor_{0};
        std::size_t cached_tx_ack_{0};

        alignas(64) std::size_t local_rx_cursor_{0};
        std::size_t cached_rx_gate_{0};
        
        // Structural Hoisting Pointers
        std::atomic<std::size_t>* remote_tx_ack_{nullptr};
        std::atomic<std::size_t>* local_tx_gate_{nullptr};
        zero_copy_descriptor*     local_tx_slots_{nullptr};
        
        std::atomic<std::size_t>* remote_rx_gate_{nullptr};
        std::atomic<std::size_t>* local_rx_ack_{nullptr};
        zero_copy_descriptor*     local_rx_slots_{nullptr};

    public:
        explicit shm_arena_duplex(std::string_view prefix, bool primary, uint8_t* arena_base)
        : arena_base_ptr_(arena_base), is_primary_(primary) {
            core::slab_anchor_assert(arena_base_ptr_);

            std::snprintf(c_name_, sizeof(c_name_), "/%.*s_ar_ctrl", static_cast<int>(prefix.size()), prefix.data());
            std::snprintf(d_name_, sizeof(d_name_), "/%.*s_ar_data", static_cast<int>(prefix.size()), prefix.data());

            if (is_primary_) {
                ::shm_unlink(c_name_); ::shm_unlink(d_name_); // Clean up stale segments
                ctrl_fd_ = ::shm_open(c_name_, O_CREAT | O_RDWR, 0666);
                data_fd_ = ::shm_open(d_name_, O_CREAT | O_RDWR, 0666);

                if (ctrl_fd_ < 0 || data_fd_ < 0) {
                    throw std::runtime_error("Failed to open arena SHM control/data descriptors.");
                }

                if (::ftruncate(ctrl_fd_, sizeof(shm_control_block)) != 0 || 
                    ::ftruncate(data_fd_, sizeof(shm_data_matrix)) != 0) {
                    throw std::runtime_error("Failed to truncate arena SHM segments.");
                }
            } else {
                ctrl_fd_ = ::shm_open(c_name_, O_RDWR, 0666);
                data_fd_ = ::shm_open(d_name_, O_RDWR, 0666);
                if (ctrl_fd_ < 0 || data_fd_ < 0) {
                    throw std::runtime_error("Joiner failed to open existing arena SHM segments.");
                }
            }

            ctrl_ = static_cast<shm_control_block*>(::mmap(nullptr, sizeof(shm_control_block), PROT_READ | PROT_WRITE, MAP_SHARED, ctrl_fd_, 0));
            data_ = static_cast<shm_data_matrix*>(::mmap(nullptr, sizeof(shm_data_matrix), PROT_READ | PROT_WRITE, MAP_SHARED, data_fd_, 0));
            
            if (ctrl_ == MAP_FAILED || data_ == MAP_FAILED) {
                throw std::runtime_error("mmap of arena SHM segments failed.");
            }

            if (is_primary_) {
                std::memset(ctrl_, 0, sizeof(shm_control_block));
                std::memset(data_, 0, sizeof(shm_data_matrix));
                
                remote_tx_ack_ = &ctrl_->physical_rx_ack;
                local_tx_gate_ = &ctrl_->physical_tx_gate;
                local_tx_slots_ = data_->tx_slots;

                remote_rx_gate_ = &ctrl_->physical_tx_gate;
                local_rx_ack_ = &ctrl_->physical_rx_ack;
                local_rx_slots_ = data_->rx_slots;
            } else {
                remote_tx_ack_ = &ctrl_->physical_tx_ack;
                local_tx_gate_ = &ctrl_->physical_rx_gate;
                local_tx_slots_ = data_->rx_slots;

                remote_rx_gate_ = &ctrl_->physical_rx_gate;
                local_rx_ack_ = &ctrl_->physical_tx_ack;
                local_rx_slots_ = data_->tx_slots;
            }
        }

        ~shm_arena_duplex() noexcept {
            if (ctrl_ && ctrl_ != MAP_FAILED) ::munmap(ctrl_, sizeof(shm_control_block));
            if (data_ && data_ != MAP_FAILED) ::munmap(data_, sizeof(shm_data_matrix));
            if (ctrl_fd_ != -1) ::close(ctrl_fd_);
            if (data_fd_ != -1) ::close(data_fd_);
            if (is_primary_) {
                ::shm_unlink(c_name_);
                ::shm_unlink(d_name_);
            }
        }

        template <typename OutboundConduit>
        SLAB_FORCE_INLINE void process_egress_burst(OutboundConduit& conduit) noexcept {
            // Shadow Pointer Step 1: Check local L1 register view
            if (local_tx_cursor_ - cached_tx_ack_ >= Capacity) [[unlikely]] {
                // Ack Sync: Pull the remote horizon only on congestion
                cached_tx_ack_ = remote_tx_ack_->load(std::memory_order_acquire);
                if (local_tx_cursor_ - cached_tx_ack_ >= Capacity) return;
            }

            std::size_t burst = std::min(Capacity - (local_tx_cursor_ - cached_tx_ack_), 32ul);
            alignas(64) core::tagged_pointer batch[32];
            std::size_t pulled = conduit.pop_batch(batch, burst);
            if (pulled == 0) return;

            auto* slots = local_tx_slots_;

            for (std::size_t i = 0; i < pulled; ++i) {
                auto* absolute_ptr = reinterpret_cast<uint8_t*>(batch[i].get_address());
                std::size_t idx = (local_tx_cursor_ + i) & MASK;

                // Efficient address translation for shared memory relocatability.
                slots[idx].shm_offset = static_cast<uint32_t>(absolute_ptr - arena_base_ptr_);
                slots[idx].sequence_number = batch[i].tag();
            }

            local_tx_cursor_ += pulled;
            local_tx_gate_->store(local_tx_cursor_, std::memory_order_release);
        }
    };
}
