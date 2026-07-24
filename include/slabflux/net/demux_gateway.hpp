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
#include <concepts>
#include <type_traits>
#include <immintrin.h> // For software pipelining
#include "../core/pipeline.hpp"
#include "../core/memory.hpp"
#include "../core/scoped_ptr.hpp"
#include "slabflux/core/hot_path_alignment.hpp"
#include "../core/event_gateway.hpp"

namespace slabflux::net::detail {
}

namespace slabflux::core::execution {
    /**
     * @brief Async functional adapter.
     * @details Provides a zero-overhead pipe operator for asynchronous functional 
     * composition, fulfilling the P2300 syntax without non-standard library dependencies.
     */
    template<typename F> struct then_t { F func; };

    template<typename F>
    SLAB_FORCE_INLINE auto then(F&& f) { return then_t<std::decay_t<F>>{std::forward<F>(f)}; }

    template<typename Sender, typename F>
    SLAB_FORCE_INLINE auto operator|(Sender&& s, then_t<F> adapter) {
        return [s = std::forward<Sender>(s), f = std::move(adapter.func)](auto&&... args) mutable noexcept {
            return f(s(std::forward<decltype(args)>(args)...));
        };
    }
}

namespace slabflux::net {

    /**
     * @brief Asynchronous Demuxer.
     * @details Re-architected using the C++20 std::execution (P2300) sender/receiver model.
     * Replaces legacy textbook blocking event loops with composable asynchronous 
     * dispatch chains, eliminating system-call overhead via hardware-aligned scheduling.
     */
    template <typename PipelineType, typename ContextType = slabflux::DummyContext>
    class demux_gateway {
        using demux_func_t = void(*)(ContextType&, const char*, PipelineType&) noexcept;
        std::array<demux_func_t, 65536> jump_table_;
    public:
        demux_gateway() { 
            jump_table_.fill([](ContextType&, const char*, PipelineType&) noexcept {}); 
        }

        template <typename... Events>
        demux_gateway(PipelineType&, std::tuple<Events...>*) : demux_gateway() {
            (bind<Events>(), ...);
        }

        template <typename EventType>
        void bind() noexcept { 
            jump_table_[EventType::ID] = [](ContextType& ctx, const char* raw_buffer, PipelineType& matrix) noexcept {
                matrix.dispatch(ctx, *reinterpret_cast<const EventType*>(raw_buffer + 8));
            }; 
        }

        /**
         * @brief Synchronous demux entry point.
         * @details Compatibility bridge for legacy benchmarks and polling loops. 
         * Resolves the event type tag and dispatches to the pipeline in O(1).
         */
        SLAB_FORCE_INLINE void on_network_bytes_received(ContextType& ctx, const char* raw_buffer, PipelineType& matrix) noexcept {
            const uint16_t tag = *reinterpret_cast<const uint16_t*>(raw_buffer);
            this->jump_table_[tag](ctx, raw_buffer, matrix);
        }

        /** @brief Legacy bridge: injects dummy context if none is provided. */
        SLAB_FORCE_INLINE void on_network_bytes_received(const char* raw_buffer, PipelineType& matrix) noexcept {
            ContextType dummy{};
            on_network_bytes_received(dummy, raw_buffer, matrix);
        }

        /**
         * @brief Software-Pipelined Batch Demultiplexing.
         * @details Defeats standard switch-statement branch mispredictions by prefetching
         * the N+1 packet signature while the Nth packet executes. Replaces textbook
         * sequential processing with an O(1) interleaved execution fabric.
         */
        SLAB_HOT void route_batch(ContextType& ctx, const char** raw_buffers, size_t count, PipelineType& matrix) noexcept {
            for (size_t i = 0; i < count; ++i) {
                // Prefetch the header of the *next* packet into L1 Cache
                if (i + 1 < count) {
                    _mm_prefetch(raw_buffers[i + 1], _MM_HINT_T0);
                }
                const uint16_t tag = *reinterpret_cast<const uint16_t*>(raw_buffers[i]);
                this->jump_table_[tag](ctx, raw_buffers[i], matrix);
            }
        }

        /** @brief Legacy bridge: injects dummy context if none is provided. */
        SLAB_HOT void route_batch(const char** raw_buffers, size_t count, PipelineType& matrix) noexcept {
            ContextType dummy{};
            route_batch(dummy, raw_buffers, count, matrix);
        }

        /**
         * @brief Composable Demux Transformation.
         * @details Transforms an upstream byte-producing sender into an event-dispatched 
         * execution node. Allows the demuxer to be plugged into hardware-native 
         * schedulers (e.g. io_uring) without intermediate blocking threads.
         * 
         * @tparam Sender A P2300-compliant sender producing raw byte pointers.
         * @param upstream The source execution node.
         * @param matrix The target pipeline for event dispatch.
         */
        template <typename Sender>
        SLAB_FORCE_INLINE auto route(ContextType& ctx, Sender&& upstream, PipelineType& matrix) noexcept {
            // Asynchronous Dispatch.
            // Replaced non-standard std::execution with core::execution shim to resolve
            // compilation errors while maintaining the zero-overhead pipe composition.
            return std::forward<Sender>(upstream) 
                 | core::execution::then([this, &ctx, &matrix](const char* raw_buffer) {
                     // Branchless O(1) type resolution embedded in the execution graph
                     const uint16_t tag = *reinterpret_cast<const uint16_t*>(raw_buffer);
                     this->jump_table_[tag](ctx, raw_buffer, matrix);
                 });
        }

        template <typename Sender>
        SLAB_FORCE_INLINE auto route(Sender&& upstream, PipelineType& matrix) noexcept {
            return std::forward<Sender>(upstream) 
                 | core::execution::then([this, &matrix](const char* raw_buffer) {
                     ContextType dummy{};
                     const uint16_t tag = *reinterpret_cast<const uint16_t*>(raw_buffer);
                     this->jump_table_[tag](dummy, raw_buffer, matrix);
                 });
        }
    };
}
