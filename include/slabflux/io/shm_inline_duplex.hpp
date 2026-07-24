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
 * @details Engineered for micro-bursts of small payloads (< 256 bytes).
 * Utilizes direct linear memory mapping to maximize hardware prefetcher
 * efficiency and eliminate TLB misses during inter-process routing.
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string_view>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/memory.hpp"
#include "slabflux/transport/wire_protocol.hpp"

namespace slabflux::io {

    /**
     * @brief Atomic Layout Inter-Process Synthesizer.
     * @details Statically derives physical envelopes for inline-data SHM matrices.
     * Enforces page-aligned isolation and stride synthesis for the data matrix.
     */
    template <std::size_t Capacity, std::size_t MaxPayloadSize>
    struct shm_inline_layout_synthesizer {
        static constexpr std::size_t PAGE_SIZE   = 4096;
        static constexpr std::size_t FRAME_ALIGNED = (sizeof(transport::raw_tcp_frame) + 63) & ~std::size_t(63);
        static constexpr std::size_t SLOT_STRIDE = (FRAME_ALIGNED + MaxPayloadSize + 63) & ~std::size_t(63);
        static constexpr std::size_t CTRL_SIZE   = (64 * 4 + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        static constexpr std::size_t MATRIX_SIZE = (SLOT_STRIDE * Capacity * 2 + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    };

    template <std::size_t Capacity = 1024, std::size_t MaxPayloadSize = 128>
    class alignas(64) shm_inline_duplex {
        static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be an exact power of 2");
        using Synthesis = shm_inline_layout_synthesizer<Capacity, MaxPayloadSize>;
        static constexpr std::size_t MASK = Capacity - 1;

    public:
        struct alignas(64) shm_slot {
            transport::raw_tcp_frame frame;
            // Alignment: Payload must be 64-byte aligned to support Non-Temporal stores
            alignas(64) char payload[MaxPayloadSize];
            char padding[Synthesis::SLOT_STRIDE - Synthesis::FRAME_ALIGNED - MaxPayloadSize];
        };

        struct alignas(4096) shm_control_block {
            alignas(64) std::atomic<std::size_t> physical_tx_gate{0}; // Published Tail
            alignas(64) std::atomic<std::size_t> physical_tx_ack{0};  // Published Head
            alignas(64) std::atomic<std::size_t> physical_rx_gate{0}; // Published Tail
            alignas(64) std::atomic<std::size_t> physical_rx_ack{0};  // Published Head
        };

        struct alignas(4096) shm_data_matrix {
            shm_slot tx_slots[Capacity];
            alignas(4096) shm_slot rx_slots[Capacity];
        };

    private:
        shm_control_block* ctrl_{nullptr};
        shm_data_matrix* data_{nullptr};
        int ctrl_fd_{-1}, data_fd_{-1};
        bool is_primary_{false};

        // --------------------------------------------------------------------
        // CORE-LOCAL REGISTERS: Grouped to eliminate internal RFO stalls
        // --------------------------------------------------------------------
        alignas(64) std::size_t local_tx_cursor_{0};
        std::size_t cached_tx_ack_{0};

        alignas(64) std::size_t local_rx_cursor_{0};
        std::size_t cached_rx_gate_{0};

    public:
        explicit shm_inline_duplex(std::string_view prefix, bool primary) : is_primary_(primary) {
            // Initialization logic identical to previous standard mappings...
            char c_name[128], d_name[128];
            std::snprintf(c_name, sizeof(c_name), "/%.*s_inl_ctrl", static_cast<int>(prefix.size()), prefix.data());
            std::snprintf(d_name, sizeof(d_name), "/%.*s_inl_data", static_cast<int>(prefix.size()), prefix.data());

            if (is_primary_) {
                ::shm_unlink(c_name); ::shm_unlink(d_name); // Cleanup stale segments
                ctrl_fd_ = ::shm_open(c_name, O_CREAT | O_RDWR, 0666);
                data_fd_ = ::shm_open(d_name, O_CREAT | O_RDWR, 0666);

                if (ctrl_fd_ < 0 || data_fd_ < 0) {
                    throw std::runtime_error("Failed to open inline SHM descriptors.");
                }

                if (::ftruncate(ctrl_fd_, sizeof(shm_control_block)) != 0 ||
                    ::ftruncate(data_fd_, sizeof(shm_data_matrix)) != 0) {
                    throw std::runtime_error("Failed to truncate inline SHM segments.");
                }
            } else {
                ctrl_fd_ = ::shm_open(c_name, O_RDWR, 0666);
                data_fd_ = ::shm_open(d_name, O_RDWR, 0666);
                if (ctrl_fd_ < 0 || data_fd_ < 0) {
                    throw std::runtime_error("Joiner failed to open existing inline SHM segments.");
                }
            }

            ctrl_ = static_cast<shm_control_block*>(::mmap(nullptr, sizeof(shm_control_block), PROT_READ | PROT_WRITE, MAP_SHARED, ctrl_fd_, 0));
            data_ = static_cast<shm_data_matrix*>(::mmap(nullptr, sizeof(shm_data_matrix), PROT_READ | PROT_WRITE, MAP_SHARED, data_fd_, 0));

            if (ctrl_ == MAP_FAILED || data_ == MAP_FAILED) {
                throw std::runtime_error("mmap of inline SHM segments failed.");
            }

            if (is_primary_) {
                std::memset(ctrl_, 0, sizeof(shm_control_block));
                std::memset(data_, 0, sizeof(shm_data_matrix));
            }
        }

        ~shm_inline_duplex() noexcept {
            if (ctrl_ && ctrl_ != MAP_FAILED) ::munmap(ctrl_, sizeof(shm_control_block));
            if (data_ && data_ != MAP_FAILED) ::munmap(data_, sizeof(shm_data_matrix));
            if (ctrl_fd_ != -1) ::close(ctrl_fd_);
            if (data_fd_ != -1) ::close(data_fd_);
        }

        template <typename OutboundConduit>
        SLAB_FORCE_INLINE void process_egress_burst(OutboundConduit& conduit) noexcept {
            // Shadow Pointer Step 1: Check local L1 register view
            if (local_tx_cursor_ - cached_tx_ack_ >= Capacity) [[unlikely]] {
                // Ack Sync: Pull the remote horizon only on congestion
                cached_tx_ack_ = is_primary_ ? ctrl_->physical_tx_ack.load(std::memory_order_acquire) 
                                             : ctrl_->physical_rx_ack.load(std::memory_order_acquire);
                if (local_tx_cursor_ - cached_tx_ack_ >= Capacity) return;
            }

            std::size_t burst = std::min(Capacity - (local_tx_cursor_ - cached_tx_ack_), 32ul);
            alignas(64) core::tagged_pointer batch[32];
            std::size_t pulled = conduit.pop_batch(batch, burst);
            if (pulled == 0) return;

            // INDIRECTION HOISTING: Raw pointers into registers
            auto* __restrict__ curr_slot = is_primary_ ? &data_->tx_slots[local_tx_cursor_ & MASK] 
                                                       : &data_->rx_slots[local_tx_cursor_ & MASK];

            for (std::size_t i = 0; i < pulled; ++i) {
                const auto* __restrict__ raw = reinterpret_cast<const transport::raw_tcp_frame*>(batch[i].get_address());
                
                // HEADER FUSION: Write header from scalar register
                const uint64_t meta = (static_cast<uint64_t>(raw->connection_id) << 32) | raw->payload_length;
                *reinterpret_cast<uint64_t*>(&curr_slot->frame) = meta;

                // VECTOR PATH: Copy only 1 cache line if data fits (64B)
                // If EventType is larger, the compiler unrolls it.
                const __m512i* v_src = reinterpret_cast<const __m512i*>(raw->data);
                __m512i* v_dst = reinterpret_cast<__m512i*>(curr_slot->payload);
                
                _mm512_storeu_si512(v_dst, _mm512_loadu_si512(v_src));
                
                curr_slot++; // Raw pointer ADD (mirror image optimization)
            }

            _mm_sfence(); 

            local_tx_cursor_ += pulled;
            if (is_primary_) ctrl_->physical_tx_gate.store(local_tx_cursor_, std::memory_order_release);
            else ctrl_->physical_rx_gate.store(local_tx_cursor_, std::memory_order_release);
        }
    };
}
