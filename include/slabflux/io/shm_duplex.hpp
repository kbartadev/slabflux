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
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/memory.hpp"
#include "slabflux/transport/wire_protocol.hpp"

namespace slabflux::io {

    template <std::size_t Capacity = 512>
    class alignas(64) shm_duplex {
        static_assert((Capacity & (Capacity - 1)) == 0, "SHM Ring Capacity must be an exact power of 2");

    public:
        static constexpr std::size_t SLOT_STRIDE = 2048;
        static constexpr std::size_t MASK        = Capacity - 1;

        struct alignas(64) shm_slot {
            transport::raw_tcp_frame frame;
            char padding[SLOT_STRIDE - sizeof(transport::raw_tcp_frame)];
        };

        struct alignas(4096) shm_control_block {
            alignas(64) std::atomic<std::size_t> tx_tail{0};
            alignas(64) std::atomic<std::size_t> tx_head{0};
            alignas(64) std::atomic<std::size_t> rx_tail{0};
            alignas(64) std::atomic<std::size_t> rx_head{0};
        };

        struct alignas(4096) shm_data_matrix {
            shm_slot tx_slots[Capacity];
            alignas(4096) shm_slot rx_slots[Capacity];
        };

    private:
        int                 shm_ctrl_fd_{-1};
        int                 shm_data_fd_{-1};
        shm_control_block* ctrl_{nullptr};
        shm_data_matrix* data_{nullptr};
        bool                is_primary_{false};
        char                ctrl_name_[128]{0};
        char                data_name_[128]{0};

        alignas(64) std::size_t local_tx_tail_{0};
        alignas(64) std::size_t local_tx_head_{0};
        alignas(64) std::size_t local_rx_head_{0};
        alignas(64) std::size_t local_rx_tail_{0};
        
        // Structural Hoisting Pointers to eliminate primary vs secondary branching
        std::atomic<std::size_t>* remote_tx_head_{nullptr};
        std::atomic<std::size_t>* local_tx_tail_ptr_{nullptr};
        shm_slot*                 local_tx_slots_{nullptr};

        std::atomic<std::size_t>* remote_rx_tail_{nullptr};
        std::atomic<std::size_t>* local_rx_head_ptr_{nullptr};
        shm_slot*                 local_rx_slots_{nullptr};

    public:
        explicit shm_duplex(std::string_view name_prefix, bool is_primary)
        : is_primary_(is_primary)
        {
            std::snprintf(ctrl_name_, sizeof(ctrl_name_), "/%.*s_ctrl", static_cast<int>(name_prefix.size()), name_prefix.data());
            std::snprintf(data_name_, sizeof(data_name_), "/%.*s_data", static_cast<int>(name_prefix.size()), name_prefix.data());

            if (is_primary_) {
                ::shm_unlink(ctrl_name_);
                ::shm_unlink(data_name_);
                shm_ctrl_fd_ = ::shm_open(ctrl_name_, O_CREAT | O_RDWR | O_EXCL, 0666);
                shm_data_fd_ = ::shm_open(data_name_, O_CREAT | O_RDWR | O_EXCL, 0666);

                if (shm_ctrl_fd_ < 0 || shm_data_fd_ < 0) {
                    throw std::runtime_error("Failed to create primary SHM segments. Check /dev/shm for stale files.");
                }

                if (::ftruncate(shm_ctrl_fd_, sizeof(shm_control_block)) != 0 ||
                    ::ftruncate(shm_data_fd_, sizeof(shm_data_matrix)) != 0) {
                    throw std::runtime_error("Failed to resize SHM segments.");
                }
            } else {
                shm_ctrl_fd_ = ::shm_open(ctrl_name_, O_RDWR, 0666);
                shm_data_fd_ = ::shm_open(data_name_, O_RDWR, 0666);
                if (shm_ctrl_fd_ < 0 || shm_data_fd_ < 0) {
                    throw std::runtime_error("Failed to join existing SHM segments.");
                }
            }

            ctrl_ = static_cast<shm_control_block*>(::mmap(nullptr, sizeof(shm_control_block), PROT_READ | PROT_WRITE, MAP_SHARED, shm_ctrl_fd_, 0));
            data_ = static_cast<shm_data_matrix*>(::mmap(nullptr, sizeof(shm_data_matrix), PROT_READ | PROT_WRITE, MAP_SHARED, shm_data_fd_, 0));

            if (ctrl_ == MAP_FAILED || data_ == MAP_FAILED) {
                throw std::runtime_error("mmap of SHM segments failed.");
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

        ~shm_duplex() noexcept {
            if (ctrl_ && ctrl_ != MAP_FAILED) ::munmap(ctrl_, sizeof(shm_control_block));
            if (data_ && data_ != MAP_FAILED) ::munmap(data_, sizeof(shm_data_matrix));
            if (shm_ctrl_fd_ != -1) ::close(shm_ctrl_fd_);
            if (shm_data_fd_ != -1) ::close(shm_data_fd_);
            if (is_primary_) {
                ::shm_unlink(ctrl_name_);
                ::shm_unlink(data_name_);
            }
        }

        template <typename OutboundConduit, typename PoolType>
        SLAB_FORCE_INLINE void process_egress_burst(OutboundConduit& egress_conduit, PoolType& pool) noexcept {
            const std::size_t current_tail = local_tx_tail_;

            if (current_tail - local_tx_head_ >= Capacity) [[unlikely]] {
                local_tx_head_ = remote_tx_head_->load(std::memory_order_acquire);
                if (current_tail - local_tx_head_ >= Capacity) return;
            }

            std::size_t max_fillable = Capacity - (current_tail - local_tx_head_);
            std::size_t desired_burst = (max_fillable < 32) ? max_fillable : 32;

            alignas(64) core::tagged_pointer batch[32];
            std::size_t pulled = egress_conduit.pop_batch(batch, desired_burst);

            if (__builtin_expect(pulled == 0, 1)) return;

            auto* target_slots = local_tx_slots_;
            transport::raw_tcp_frame* release_ptrs[32];

            for (std::size_t i = 0; i < pulled; ++i) {
                auto* raw_packet = reinterpret_cast<transport::raw_tcp_frame*>(batch[i].get_address());
                std::size_t target_idx = (current_tail + i) & MASK;

                // Metadata Fusion: 1-cycle atomic meta update.
                const uint64_t meta = (static_cast<uint64_t>(raw_packet->connection_id) << 32) | 
                                       static_cast<uint64_t>(raw_packet->payload_length);
                *reinterpret_cast<uint64_t*>(&target_slots[target_idx].frame) = meta;

                // IPC PATH: Non-Temporal Matrix Streaming.
                // Replaces standard memcpy with cache-bypassing stores.
                // Prevents the Compute Core's L1-L3 from being polluted by IPC egress traffic.
                const char* src_data = raw_packet->data;
                char* dst_data = target_slots[target_idx].frame.data;
                size_t bytes_to_copy = raw_packet->payload_length;
                
                #pragma unroll
                for(size_t v = 0; v < bytes_to_copy / 64; ++v) {
                    _mm512_storeu_si512(reinterpret_cast<__m512i*>(dst_data + v * 64), _mm512_loadu_si512(reinterpret_cast<const __m512i*>(src_data + v * 64)));
                }
                // Handle remaining bytes if not a multiple of 64
                if (bytes_to_copy % 64 != 0) {
                    std::memcpy(dst_data + (bytes_to_copy / 64) * 64, src_data + (bytes_to_copy / 64) * 64, bytes_to_copy % 64);
                }
                release_ptrs[i] = raw_packet;
            }

            _mm_sfence(); // Amortized fence: Ensure SHM visibility for the entire burst
            pool.release_batch(release_ptrs, pulled);

            local_tx_tail_ += pulled;

            // Single amortized atomic release for the entire batch
            local_tx_tail_ptr_->store(local_tx_tail_, std::memory_order_release);
        }

        template <typename InboundConduit>
        SLAB_FORCE_INLINE void process_ingress_burst(InboundConduit& ingress_conduit) noexcept {
            const std::size_t current_head = local_rx_head_;

            if (current_head == local_rx_tail_) {
                local_rx_tail_ = remote_rx_tail_->load(std::memory_order_acquire);
                if (current_head == local_rx_tail_) return;
            }

            std::size_t available = local_rx_tail_ - current_head;
            std::size_t burst_count = (available < 32) ? available : 32;

            alignas(64) typename InboundConduit::value_type token_batch[32];
            auto* source_slots = local_rx_slots_;

            for (std::size_t i = 0; i < burst_count; ++i) {
                std::size_t src_idx = (current_head + i) & MASK;
                auto packed = core::tagged_pointer::pack(
                    static_cast<uint16_t>(src_idx),
                                                            &source_slots[src_idx].frame
                );
                if constexpr (requires { token_batch[i].embed_symmetry(0); }) {
                    token_batch[i] = typename InboundConduit::value_type(transport::raw_tcp_frame::ID, packed);
                    token_batch[i].embed_symmetry(static_cast<uint32_t>(local_rx_head_ + i));
                } else if constexpr (requires { token_batch[i].anchor_to_lightcone(0); }) {
                    token_batch[i] = typename InboundConduit::value_type(packed);
                    token_batch[i].anchor_to_lightcone(local_rx_head_ + i);
                } else {
                    token_batch[i] = packed;
                }
            }

            std::size_t pushed = ingress_conduit.push_batch(token_batch, burst_count);
            local_rx_head_ += pushed;

            local_rx_head_ptr_->store(local_rx_head_, std::memory_order_release);
        }
    };
}
