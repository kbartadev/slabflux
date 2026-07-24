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
#include <string_view>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/memory.hpp"

namespace slabflux::io {

    template <std::size_t Capacity = 512>
    class alignas(64) shm_journal_duplex {
        static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");

    public:
        struct alignas(64) zero_copy_descriptor {
            uint32_t shm_offset;       // Distance from the Journal/Arena base_ptr.
            uint16_t connection_id;
            uint16_t padding_flags;
            uint64_t sequence_number;
            char padding[48];          // L1 Cache-line padding.
        };

        struct alignas(4096) shm_control_block {
            alignas(64) std::atomic<std::size_t> tx_tail{0};
            alignas(64) std::atomic<std::size_t> tx_head{0};
            alignas(64) std::atomic<std::size_t> rx_tail{0};
            alignas(64) std::atomic<std::size_t> rx_head{0};
        };

        struct alignas(4096) shm_data_matrix {
            zero_copy_descriptor tx_slots[Capacity];
            alignas(4096) zero_copy_descriptor rx_slots[Capacity];
        };

        int shm_ctrl_fd_{-1}, shm_data_fd_{-1};
        shm_control_block* ctrl_{nullptr};
        shm_data_matrix* data_{nullptr};
        bool is_primary_{false};

        // Shared memory arena base address (provided by the Journal).
        uint8_t* local_arena_base_{nullptr};

        alignas(64) std::size_t local_tx_tail_{0}, local_tx_head_{0};
        alignas(64) std::size_t local_rx_head_{0}, local_rx_tail_{0};
        
        std::atomic<std::size_t>* remote_tx_head_{nullptr};
        std::atomic<std::size_t>* local_tx_tail_ptr_{nullptr};
        zero_copy_descriptor*     local_tx_slots_{nullptr};
        
        std::atomic<std::size_t>* remote_rx_tail_{nullptr};
        std::atomic<std::size_t>* local_rx_head_ptr_{nullptr};
        zero_copy_descriptor*     local_rx_slots_{nullptr};

    public:
        // Constructor requires the base address of the persistent journal arena.
        explicit shm_journal_duplex(std::string_view name_prefix, bool is_primary, uint8_t* arena_base)
        : is_primary_(is_primary), local_arena_base_(arena_base)
        {
            /* Standard shared memory segment initialization. */
            char ctrl_name[128], data_name[128];
            std::snprintf(ctrl_name, sizeof(ctrl_name), "/%.*s_jnl_ctrl", static_cast<int>(name_prefix.size()), name_prefix.data());
            std::snprintf(data_name, sizeof(data_name), "/%.*s_jnl_data", static_cast<int>(name_prefix.size()), name_prefix.data());

            if (is_primary_) {
                ::shm_unlink(ctrl_name); ::shm_unlink(data_name);
                shm_ctrl_fd_ = ::shm_open(ctrl_name, O_CREAT | O_RDWR, 0666);
                shm_data_fd_ = ::shm_open(data_name, O_CREAT | O_RDWR, 0666);

                if (shm_ctrl_fd_ < 0 || shm_data_fd_ < 0) {
                    throw std::runtime_error("Failed to create journal SHM segments.");
                }

                if (::ftruncate(shm_ctrl_fd_, sizeof(shm_control_block)) != 0 ||
                    ::ftruncate(shm_data_fd_, sizeof(shm_data_matrix)) != 0) {
                    throw std::runtime_error("Failed to truncate journal SHM segments.");
                }
            } else {
                shm_ctrl_fd_ = ::shm_open(ctrl_name, O_RDWR, 0666);
                shm_data_fd_ = ::shm_open(data_name, O_RDWR, 0666);
                if (shm_ctrl_fd_ < 0 || shm_data_fd_ < 0) {
                    throw std::runtime_error("Joiner failed to open existing journal SHM segments.");
                }
            }

            ctrl_ = static_cast<shm_control_block*>(::mmap(nullptr, sizeof(shm_control_block), PROT_READ | PROT_WRITE, MAP_SHARED, shm_ctrl_fd_, 0));
            data_ = static_cast<shm_data_matrix*>(::mmap(nullptr, sizeof(shm_data_matrix), PROT_READ | PROT_WRITE, MAP_SHARED, shm_data_fd_, 0));
            
            if (ctrl_ == MAP_FAILED || data_ == MAP_FAILED) {
                throw std::runtime_error("mmap of journal SHM segments failed.");
            }

            if (is_primary_) {
                std::memset(ctrl_, 0, sizeof(shm_control_block));
                std::memset(data_, 0, sizeof(shm_data_matrix));
                
                remote_tx_head_ = &ctrl_->rx_head;
                local_tx_tail_ptr_ = &ctrl_->tx_tail;
                local_tx_slots_ = data_->tx_slots;

                remote_rx_tail_ = &ctrl_->tx_tail;
                local_rx_head_ptr_ = &ctrl_->rx_head;
                local_rx_slots_ = data_->rx_slots;
            } else {
                remote_tx_head_ = &ctrl_->tx_head;
                local_tx_tail_ptr_ = &ctrl_->rx_tail;
                local_tx_slots_ = data_->rx_slots;

                remote_rx_tail_ = &ctrl_->rx_head;
                local_rx_head_ptr_ = &ctrl_->tx_head;
                local_rx_slots_ = data_->tx_slots;
            }
        }

        template <typename OutboundConduit>
        SLAB_FORCE_INLINE void process_egress_burst(OutboundConduit& egress_conduit) noexcept {
            const std::size_t current_tail = local_tx_tail_;
            if (current_tail - local_tx_head_ >= Capacity) [[unlikely]] {
                local_tx_head_ = remote_tx_head_->load(std::memory_order_acquire);
                if (current_tail - local_tx_head_ >= Capacity) return;
            }

            std::size_t burst = std::min(Capacity - (current_tail - local_tx_head_), 32ul);
            alignas(64) core::tagged_pointer batch[32];
            std::size_t pulled = egress_conduit.pop_batch(batch, burst);

            auto* target_slots = local_tx_slots_;

            for (std::size_t i = 0; i < pulled; ++i) {
                const std::size_t idx = (current_tail + i) & (Capacity - 1);
                
                // Address Transcoder: 1-cycle pointer-to-offset reduction. Replaces absolute
                // virtual addresses with arena-relative offsets to ensure bit-perfect
                // cross-process relocatability.
                const uintptr_t raw_val = batch[i].data;
                const uint32_t offset = static_cast<uint32_t>(
                    (raw_val & core::tagged_pointer::PTR_MASK) - reinterpret_cast<uintptr_t>(local_arena_base_)
                );

                target_slots[idx].shm_offset = offset;
                target_slots[idx].sequence_number = static_cast<uint64_t>(raw_val >> 48);
            }

            local_tx_tail_ += pulled;
            local_tx_tail_ptr_->store(local_tx_tail_, std::memory_order_release);
        }

        template <typename InboundConduit>
        SLAB_FORCE_INLINE void process_ingress_burst(InboundConduit& ingress_conduit) noexcept {
            const std::size_t current_head = local_rx_head_;
            if (current_head == local_rx_tail_) {
                local_rx_tail_ = remote_rx_tail_->load(std::memory_order_acquire);
                if (current_head == local_rx_tail_) return;
            }

            std::size_t burst = std::min(local_rx_tail_ - current_head, 32ul);
            alignas(64) typename InboundConduit::value_type batch[32];
            auto* source_slots = local_rx_slots_;

            for (std::size_t i = 0; i < burst; ++i) {
                std::size_t idx = (current_head + i) & (Capacity - 1);

                // OFFSET TO POINTER resolution (1 cycle addition, ZERO COPY).
                uint8_t* resolved_ptr = local_arena_base_ + source_slots[idx].shm_offset;

                auto packed = core::tagged_pointer::pack(
                    static_cast<uint16_t>(source_slots[idx].sequence_number),
                                                      resolved_ptr
                );
                if constexpr (requires { batch[i].embed_symmetry(0); }) {
                    batch[i] = typename InboundConduit::value_type(transport::raw_tcp_frame::ID, packed);
                    batch[i].embed_symmetry(static_cast<uint32_t>(local_rx_head_ + i));
                } else if constexpr (requires { batch[i].anchor_to_lightcone(0); }) {
                    batch[i] = typename InboundConduit::value_type(packed);
                    batch[i].anchor_to_lightcone(local_rx_head_ + i);
                } else {
                    batch[i] = packed;
                }
            }

            local_rx_head_ += ingress_conduit.push_batch(batch, burst);
            local_rx_head_ptr_->store(local_rx_head_, std::memory_order_release);
        }
    };
}
