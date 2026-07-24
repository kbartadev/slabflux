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
#include <vector>
#include <cstddef>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h> // For AVX2 intrinsics
#include <immintrin.h>
#endif

#include "slabflux/core.hpp"
#include "slabflux/transport/session_context.hpp"
#include "slabflux/transport/tcp_stream_defragmenter.hpp"

#include "slabflux/core/demuxer.hpp" // For core::demuxer
namespace slabflux::core {

/**
 * @brief Universal Compile-Time Optimized Execution Cell for TCP Stream Processing.
 * @tparam ProtocolParser Static DFA scanner engine (e.g., baremetal_parser).
 * @tparam ProtocolState Internal frame tracking structure (e.g., http_frame).
 * @tparam BusinessLogic High-level domain application handler routing target.
 * @tparam InboundEnvelope Concrete network packet layout (e.g., raw_tcp_frame).
 */
template <
    typename ProtocolParser,
    typename ProtocolState,
    typename BusinessLogic,
    typename InboundEnvelope
>
class pipeline_lane {
private:
    using DefragmenterType =
        transport::tcp_stream_defragmenter<
            ProtocolParser,
            ProtocolState,
            BusinessLogic,
            InboundEnvelope
        >;

    // Isolated safe heap allocation registry remains cache-isolated inside the lane layout
    alignas(64) transport::session_storage_registry<ProtocolState, 1024> session_registry_;

    BusinessLogic&   application_logic_;
    DefragmenterType defragmenter_;

public:
    explicit pipeline_lane(BusinessLogic& logic) noexcept
        : application_logic_(logic)
        , defragmenter_(application_logic_, session_registry_) {}

    ~pipeline_lane() noexcept = default;

    pipeline_lane(const pipeline_lane&) = delete;
    pipeline_lane& operator=(const pipeline_lane&) = delete;

    /**
     * @brief Synchronously flushes data based on the chosen compile-time routing configuration.
     * @tparam RouteTopologyPolicy Accepts any compliant topology route trait.
     */
    template <typename RouteTopologyPolicy>
    inline bool execute_tick(RouteTopologyPolicy& topology_route) noexcept {
        using DemuxerType = slabflux::core::demuxer<DefragmenterType>;
        DemuxerType core_demuxer(defragmenter_);

        alignas(64) core::tagged_pointer batch[32];
        
        // The policy performs ONLY data gathering. Complete decoupling.
        std::size_t read_count = topology_route.consume_batch_count(batch, 32);
        if (__builtin_expect(read_count == 0, 1)) {
            return false;
        }

        core::tagged_pointer* __restrict__ local_batch = batch;

        #if defined(__clang__) || defined(__GNUC__)
        #pragma GCC unroll 32
        #endif
        for (std::size_t i = 0; i < read_count; ++i) {
            core_demuxer.dispatch(local_batch[i]);
        }
        return true;
    }

    /**
     * @brief Synchronously executes a high-speed AVX vector stream processing pass.
     */
    template <typename RouteTopologyPolicy>
    inline bool execute_vector_stream(RouteTopologyPolicy& topology_route) noexcept {
        using DemuxerType = slabflux::core::demuxer<DefragmenterType>;
        DemuxerType core_demuxer(defragmenter_);
        
        alignas(64) core::tagged_pointer batch[32];

        std::size_t read_count = topology_route.consume_batch_count(batch, 32);
        if (__builtin_expect(read_count == 0, 1)) {
            return false;
        }

        std::size_t i = 0;
        const std::size_t vectorized_chunks = read_count & ~std::size_t{3};

        // Stream raw tokens natively 4-at-a-time using vector registers inside the lane bounds
        for (; i < vectorized_chunks; i += 4) {
#if defined(__AVX2__)
            __m256i simd_vector = _mm256_load_si256(reinterpret_cast<const __m256i*>(&batch[i]));
            core_demuxer.dispatch_simd(simd_vector);
#else
            core_demuxer.dispatch(batch[i + 0]);
            core_demuxer.dispatch(batch[i + 1]);
            core_demuxer.dispatch(batch[i + 2]);
            core_demuxer.dispatch(batch[i + 3]);
#endif
        }

        for (; i < read_count; ++i) {
            core_demuxer.dispatch(batch[i]);
        }
        return true;
    }
};

// ============================================================================
// THE COMPONENT EXTENSION (THREADLESS STEP 2 BUSINESS SPECIALIZATION)
// ============================================================================
template <typename BusinessLogic>
class pipeline_lane<void, void, BusinessLogic, core::tagged_pointer> {
private:
    BusinessLogic& application_logic_;

public:
    explicit pipeline_lane(BusinessLogic& logic) noexcept
        : application_logic_(logic) {}

    ~pipeline_lane() noexcept = default;

    pipeline_lane(const pipeline_lane&) = delete;
    pipeline_lane& operator=(const pipeline_lane&) = delete;

    /**
     * @brief Synchronously executes a classic downstream scalar batch pass.
     */
    template <typename RouteTopologyPolicy>
    inline bool execute_tick(RouteTopologyPolicy& topology_route) noexcept {
        using DemuxerType = slabflux::core::demuxer<BusinessLogic>;
        DemuxerType core_demuxer(application_logic_);

        alignas(64) core::tagged_pointer batch[32];
        
        std::size_t read_count = topology_route.consume_batch_count(batch, 32);
        if (__builtin_expect(read_count == 0, 1)) {
            return false;
        }

        core::tagged_pointer* __restrict__ local_batch = batch;

        #if defined(__clang__) || defined(__GNUC__)
        #pragma GCC unroll 32
        #endif
        for (std::size_t i = 0; i < read_count; ++i) {
            core_demuxer.dispatch(local_batch[i]);
        }
        return true;
    }

    /**
     * @brief Synchronously executes a downstream AVX vector stream processing pass.
     */
    template <typename RouteTopologyPolicy>
    inline bool execute_vector_stream(RouteTopologyPolicy& topology_route) noexcept {
        using DemuxerType = slabflux::core::demuxer<BusinessLogic>;
        DemuxerType core_demuxer(application_logic_);

        alignas(64) core::tagged_pointer batch[32];
        
        std::size_t read_count = topology_route.consume_batch_count(batch, 32);
        if (__builtin_expect(read_count == 0, 1)) {
            return false;
        }

        std::size_t i = 0;
        const std::size_t vectorized_chunks = read_count & ~std::size_t{3};

        for (; i < vectorized_chunks; i += 4) {
#if defined(__AVX2__)
            __m256i simd_vector = _mm256_load_si256(reinterpret_cast<const __m256i*>(&batch[i]));
            core_demuxer.dispatch_simd(simd_vector);
#else
            core_demuxer.dispatch(batch[i + 0]);
            core_demuxer.dispatch(batch[i + 1]);
            core_demuxer.dispatch(batch[i + 2]);
            core_demuxer.dispatch(batch[i + 3]);
#endif
        }

        for (; i < read_count; ++i) {
            core_demuxer.dispatch(batch[i]);
        }
        return true;
    }
};

} // namespace slabflux::core