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
#include <cstdint>
#include <tuple>
#include "pipeline.hpp"
#include "memory.hpp"
#include "scoped_ptr.hpp"

namespace slabflux::core::detail {
    template <typename PipelineType>
    void drop_packet_adapter(const char*, PipelineType&) noexcept {}

    template <typename EventType, typename PipelineType>
    void mapped_packet_adapter(const char* raw_buffer, PipelineType& matrix) noexcept {
        matrix.dispatch(*reinterpret_cast<const EventType*>(raw_buffer + 8));
    }
}

namespace slabflux::core {
    template<typename T>
    struct event_ref {
        T* ptr_;
        T* operator->() const { return ptr_; }
    };
    template<typename Pool, typename Conduit = void>
    class event_gateway {
        Pool& pool_;
        Conduit* bus_ptr_{nullptr};

    public:
        explicit event_gateway(Pool& pool) noexcept : pool_(pool) {}
        
        template<typename C = Conduit>
        requires (!std::is_void_v<C>)
        event_gateway(Pool& pool, C& bus) noexcept : pool_(pool), bus_ptr_(&bus) {}

        /** 
         * @brief Vectorized Egress.
         * @details Drains a batch of events and executes logic in a single instruction burst, maximizing I-Cache residency.
         */
        template<typename EventType, size_t BatchSize = 16, typename F>
        SLAB_HOT void drain(F&& func) noexcept {
            static_assert(!std::is_void_v<Conduit>, "drain() requires a bound conduit");
            EventType* batch[BatchSize];
            size_t count = bus_ptr_->pop_batch(batch, BatchSize);
            
            if (count == 0) return;

            for (size_t i = 0; i < count; ++i) {
                func(event_ref<EventType>{batch[i]});
            }

            // Collective Reclamation.
            // Returns all pointers to the pool in a single atomic transaction.
            pool_.release_batch(batch, count);
        }

        /** @brief Process an external event pointer and release it to the pool. */
        template<typename EventType, typename F>
        SLAB_FORCE_INLINE void consume(EventType* ev, F&& func) noexcept {
            if (ev) [[likely]] {
                func(event_ref<EventType>{ev});
                pool_.release(ev);
            }
        }

        /** @brief Pull from the bound conduit and consume. */
        template<typename EventType, typename F>
        SLAB_FORCE_INLINE void consume(F&& func) noexcept {
            if constexpr (!std::is_void_v<Conduit>) {
                EventType* ev = nullptr;
                if (bus_ptr_ && bus_ptr_->try_pop(ev)) {
                    func(event_ref<EventType>{ev});
                    pool_.release(ev);
                }
            }
        }

        /** @brief Ownership-Aware Publication. */
        template<typename EventType, typename... Args>
        SLAB_HOT void publish(Args&&... args) noexcept {
            if constexpr (!std::is_void_v<Conduit>) {
                auto managed = pool_.make(std::forward<Args>(args)...);
                if (managed) [[likely]] {
                    bus_ptr_->push(managed);
                }
            }
        }
    };
}
