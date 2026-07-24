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
#include <array>
#include <new> // For placement new
#include "slabflux/core/managed_data.hpp"
#include "slabflux/core/wire_frame_lsn.hpp" // For T if it's a wire_frame_lsn
#include "slabflux/core/scoped_ptr.hpp"
#include "slabflux/core/spsc_ring_conduit.hpp"
#include "slabflux/core/mpsc_hybrid_pool.hpp" // For os_memory_provider

namespace slabflux::core {

    /** @brief Concept enforcement to match the SPSC standards. */ // Renamed to SlabElementSPSCRing for clarity
    template <typename T>
    concept SlabElementSPSC = requires {
        requires std::is_destructible_v<T>;
        requires sizeof(T) > 0;
    };

    /**
     * @brief SPSC-backed Memory Pool.
     * @details Utilizes a spsc_ring_conduit to manage free pointers. This implementation 
     * is designed for strictly isolated 1:1 producer-consumer wires where memory 
     * is allocated on Core A and returned on Core B with zero cross-core contention.
     * 
     * High-Performance Design:
     * - Zero-Contention: Leverages the SPSC ring's shadow pointers to move 
     *   ownership between cores without atomic bus locking.
     * - Wait-Free: Both allocation and release are wait-free O(1) operations.
     * 
     * API Contract:
     * 1. TOPOLOGY: Strictly 1:1. Multiple allocators or releasers will cause corruption.
     * 2. ALLOCATION: Non-blocking. Returns nullptr if the free-ring is empty.
     *
     * ========================================================================
     * API REFERENCE: spsc_ring_pool<T, Capacity>
     * ========================================================================
     * 1. ALLOCATION (CORE A):
     *    - T* make_raw(...Args)      : Non-blocking free-list pop + placement new.
     *    - managed_data<T, P> make(...Args) : Ownership-integrated allocation.
     * 
     * 2. RECLAMATION (CORE B):
     *    - void release(T* ptr)      : Pushes pointer back to free-list wire.
     *    - void release(scoped_ptr&) : Explicit ownership collapse.
     * 
     * 3. TELEMETRY & DMA:
     *    - size_t capacity()         : Static object count.
     *    - T* get_raw_ptr_by_id(u32) : Direct address resolution.
     */
    template <SlabElementSPSC T, std::size_t Capacity> // Apply concept constraint
    class spsc_ring_pool : private os_memory_provider {
    public:
        using value_type = T;
        using value_type_pod = std::remove_pointer_t<T>;

    private:
        struct alignas(64) storage_node { uint8_t raw[sizeof(T)]; };
        
        storage_node* raw_memory_{nullptr};
        spsc_ring_conduit<T*, Capacity> free_ring_;

        public:
            spsc_ring_pool() : os_memory_provider() { // Call base constructor
                // Use os_memory_provider to allocate pinned/hugepage memory
                // This prevents stack overflows for large pools.
                this->allocate(Capacity * sizeof(storage_node), false /* allow fallback */);
                raw_memory_ = static_cast<storage_node*>(raw_ptr_);

                for (size_t i = 0; i < Capacity; ++i) {
                    free_ring_.push(reinterpret_cast<T*>(raw_memory_[i].raw));
                }
            }

            static void release_to_ring(void* ctx, void* raw_ptr) noexcept {
                auto* p = static_cast<spsc_ring_pool*>(ctx);
                p->release(static_cast<T*>(raw_ptr));
            }

            static void deleter_fn(void* ctx, void* raw_ptr) noexcept { release_to_ring(ctx, raw_ptr); }

            SLAB_FORCE_INLINE void release(T* ptr) noexcept {
                if constexpr (!std::is_trivially_destructible_v<T>) ptr->~T();
                free_ring_.push(ptr);
            }

            SLAB_FORCE_INLINE void release(managed_data<T, spsc_ring_pool>& item) noexcept {
                T* raw = item.release();
                if (raw) [[likely]] release(raw);
            }

            /** @brief Collapses ownership and returns memory to the free-ring. */
            SLAB_FORCE_INLINE void release(scoped_ptr<value_type_pod>& item) noexcept {
                value_type_pod* raw = item.release();
                if (raw) [[likely]] release(raw);
            }

            /** @brief Collective Reclamation: Vectorized return to the free-ring. */
            SLAB_FORCE_INLINE void release_batch(T** ptrs, size_t count) noexcept {
                if constexpr (!std::is_trivially_destructible_v<T>) {
                    for (size_t i = 0; i < count; ++i) ptrs[i]->~T();
                }
                for (size_t i = 0; i < count; ++i) free_ring_.push(ptrs[i]);
            }

            /** @brief Collective Acquisition: Batch pull from the free-ring. */
            inline size_t make_batch(T** out_ptrs, size_t count) noexcept {
                size_t actual = 0;
                while (actual < count) {
                    T* ptr = make_raw();
                    if (!ptr) break;
                    out_ptrs[actual++] = ptr;
                }
                return actual;
            }

            template <typename... Args>
            [[nodiscard]] SLAB_FORCE_INLINE T* make_raw(Args&&... args) noexcept {
                T* raw_ptr = nullptr;
                // Shadow Pointer Acquisition: Transfer ownership from the free-ring without cross-core atomic tax.
                if (free_ring_.try_pop(raw_ptr)) {
                    return new (raw_ptr) T(std::forward<Args>(args)...);
                }
                return nullptr;
            }

            /**
             * @brief Managed Allocation.
             * @return A managed_data handle utilizing ring-based reclamation.
             */
            template <typename... Args>
            [[nodiscard]] SLAB_FORCE_INLINE auto make(Args&&... args) noexcept {
                T* raw_ptr = make_raw(std::forward<Args>(args)...);
                if (!raw_ptr) [[unlikely]] {
                    return managed_data<T, spsc_ring_pool>();
                }
                return managed_data<T, spsc_ring_pool>(raw_ptr, *this);
            }

        /**
         * @brief Direct DMA mapping.
         */
        SLAB_FORCE_INLINE T* get_raw_ptr_by_id(uint32_t id) noexcept {
            return reinterpret_cast<T*>(raw_memory_[id].raw);
        }
        
        /** @brief Returns the base address of the physically hardened memory block.
         *  @details Useful for zero-copy registration with io_uring or AF_XDP.
         */
        [[nodiscard]] SLAB_FORCE_INLINE void* get_raw_ptr() noexcept { return static_cast<void*>(raw_memory_); }

        /** @brief Returns the total size of the raw memory block in bytes. */
        [[nodiscard]] SLAB_FORCE_INLINE std::size_t get_raw_ptr_size() const noexcept {
            return Capacity * sizeof(storage_node); // Assuming each node is sizeof(T)
        }

            SLAB_FORCE_INLINE std::size_t capacity() const noexcept { return Capacity; }
    };
}