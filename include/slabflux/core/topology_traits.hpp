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
#include <cstddef>
#include <array>
#include "slabflux/core/conduit.hpp"
#include "slabflux/core/memory.hpp"
#include "slabflux/core/mpmc_matrix_conduit.hpp"

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace slabflux::core {

    // ============================================================================
    // 1. PURE SPSC ROUTE (1 Producer, 1 Consumer) - ZERO OVERHEAD TICK
    // ============================================================================
    template <typename T, std::size_t Capacity>
    class spsc_route {
    private:
        core::spsc_conduit<T, Capacity>& channel_;

    public:
        explicit spsc_route(core::spsc_conduit<T, Capacity>& target_channel) noexcept 
            : channel_(target_channel) {}

        SLAB_FORCE_INLINE std::size_t consume_batch_count(T* batch_buffer, std::size_t max_count) noexcept {
            return channel_.pop_batch(batch_buffer, max_count);
        }

        template <typename DemuxerType>
        SLAB_FORCE_INLINE bool consume_batch(DemuxerType& demuxer, T* batch_buffer) noexcept {
            std::size_t read_count = channel_.pop_batch(batch_buffer, 32);
            if (__builtin_expect(read_count == 0, 1)) return false;

            for (std::size_t i = 0; i < read_count; ++i) {
                demuxer.process(batch_buffer[i]);
            }
            return true;
        }

        template <typename DemuxerType>
        SLAB_FORCE_INLINE bool consume_batch_vector(DemuxerType& demuxer, T* __restrict__ batch_buffer) noexcept {
            static_assert(sizeof(T) == 8, "Vectorized dispatch requires 8-byte primitives.");
            std::size_t read_count = channel_.pop_batch(batch_buffer, 32);
            if (__builtin_expect(read_count == 0, 1)) return false;

            std::size_t i = 0;
            const std::size_t vectorized_chunks = read_count & ~std::size_t{3};

            for (; i < vectorized_chunks; i += 4) {
#if defined(__AVX2__)
                __m256i vec_data = _mm256_load_si256(reinterpret_cast<const __m256i*>(&batch_buffer[i]));
                demuxer.dispatch_simd(vec_data);
#else
                demuxer.process(batch_buffer[i + 0]);
                demuxer.process(batch_buffer[i + 1]);
                demuxer.process(batch_buffer[i + 2]);
                demuxer.process(batch_buffer[i + 3]);
#endif
            }

            for (; i < read_count; ++i) {
                demuxer.process(batch_buffer[i]);
            }
            return true;
        }

        SLAB_FORCE_INLINE bool try_push(const T& token) noexcept {
            return channel_.try_push(token);
        }
    };

    // ============================================================================
    // 2. MPSC FAN-IN ROUTE (Multiple Producers, 1 Consumer) - UNROLLED
    // ============================================================================
    template <typename ConduitType, std::size_t UpstreamProducers>
    class mpsc_fan_in_route {
    private:
        std::array<ConduitType, UpstreamProducers>& channels_;

    public:
        explicit mpsc_fan_in_route(std::array<ConduitType, UpstreamProducers>& source_channels) noexcept 
            : channels_(source_channels) {}

        SLAB_FORCE_INLINE std::size_t consume_batch_count(typename ConduitType::value_type* batch_buffer, std::size_t max_count) noexcept {
            std::size_t total_accumulated = 0;
            for (std::size_t p = 0; p < UpstreamProducers; ++p) {
                std::size_t remaining_space = max_count - total_accumulated;
                if (__builtin_expect(remaining_space == 0, 0)) break;
                total_accumulated += channels_[p].pop_batch(&batch_buffer[total_accumulated], remaining_space);
            }
            return total_accumulated;
        }

        template <typename DemuxerType>
        SLAB_FORCE_INLINE bool consume_batch(DemuxerType& demuxer, typename ConduitType::value_type* batch_buffer) noexcept {
            std::size_t read_count = consume_batch_count(batch_buffer, 32);
            if (__builtin_expect(read_count == 0, 1)) return false;

            for (std::size_t i = 0; i < read_count; ++i) {
                demuxer.process(batch_buffer[i]);
            }
            return true;
        }

        template <typename DemuxerType>
        SLAB_FORCE_INLINE bool consume_batch_vector(DemuxerType& demuxer, typename ConduitType::value_type* __restrict__ batch_buffer) noexcept {
            std::size_t read_count = consume_batch_count(batch_buffer, 32);
            if (__builtin_expect(read_count == 0, 1)) return false;

            std::size_t i = 0;
            const std::size_t vectorized_chunks = read_count & ~std::size_t{3};

            for (; i < vectorized_chunks; i += 4) {
#if defined(__AVX2__)
                __m256i vec_data = _mm256_load_si256(reinterpret_cast<const __m256i*>(&batch_buffer[i]));
                demuxer.dispatch_simd(vec_data);
#else
                demuxer.process(batch_buffer[i + 0]);
                demuxer.process(batch_buffer[i + 1]);
                demuxer.process(batch_buffer[i + 2]);
                demuxer.process(batch_buffer[i + 3]);
#endif
            }

            for (; i < read_count; ++i) {
                demuxer.process(batch_buffer[i]);
            }
            return true;
        }
    };

    // ============================================================================
    // 3. SPMC SHARDING ROUTE (1 Producer, Multiple Consumers) - 1-CYCLE MASKING
    // ============================================================================
    template <typename ConduitType, std::size_t DownstreamConsumers>
    class spmc_shard_route {
    private:
        static_assert((DownstreamConsumers & (DownstreamConsumers - 1)) == 0, "Downstream template scaling must be a power of 2.");
        static constexpr std::size_t MASK = DownstreamConsumers - 1;

        std::array<ConduitType, DownstreamConsumers>& target_channels_;

    public:
        explicit spmc_shard_route(std::array<ConduitType, DownstreamConsumers>& target_channels) noexcept
            : target_channels_(target_channels) {}

        SLAB_FORCE_INLINE bool dispatch_token(std::size_t balance_key, typename ConduitType::value_type token) noexcept {
            std::size_t target_lane = balance_key & MASK;
            return target_channels_[target_lane].try_push(token);
        }
    };

    // ============================================================================
    // 4. MPMC CROSSBAR ROUTE (Multiple Producers, Multiple Consumers Grid Matrix)
    // ============================================================================
    template <typename ConduitType, std::size_t UpstreamProducers, std::size_t DownstreamConsumers>
    class mpmc_crossbar_route {
    private:
        static_assert((DownstreamConsumers & (DownstreamConsumers - 1)) == 0, "Downstream template scaling must be a power of 2.");
        static constexpr std::size_t MASK = DownstreamConsumers - 1;

        using GridMesh = std::array<std::array<ConduitType, UpstreamProducers>, DownstreamConsumers>;
        GridMesh& mesh_;
        std::size_t consumer_lane_id_;

    public:
        explicit mpmc_crossbar_route(GridMesh& shared_mesh, std::size_t consumer_lane_id) noexcept
            : mesh_(shared_mesh)
            , consumer_lane_id_(consumer_lane_id) {}

        SLAB_FORCE_INLINE std::size_t consume_batch_count(typename ConduitType::value_type* batch_buffer, std::size_t max_count) noexcept {
            std::size_t total_accumulated = 0;
            auto& my_private_channels = mesh_[consumer_lane_id_];

            for (std::size_t p = 0; p < UpstreamProducers; ++p) {
                std::size_t remaining_space = max_count - total_accumulated;
                if (__builtin_expect(remaining_space == 0, 0)) break;
                total_accumulated += my_private_channels[p].pop_batch(&batch_buffer[total_accumulated], remaining_space);
            }
            return total_accumulated;
        }

        template <typename DemuxerType>
        SLAB_FORCE_INLINE bool consume_batch(DemuxerType& demuxer, typename ConduitType::value_type* batch_buffer) noexcept {
            std::size_t read_count = consume_batch_count(batch_buffer, 32);
            if (__builtin_expect(read_count == 0, 1)) return false;

            for (std::size_t i = 0; i < read_count; ++i) {
                demuxer.process(batch_buffer[i]);
            }
            return true;
        }

        template <typename DemuxerType>
        SLAB_FORCE_INLINE bool consume_batch_vector(DemuxerType& demuxer, typename ConduitType::value_type* __restrict__ batch_buffer) noexcept {
            std::size_t read_count = consume_batch_count(batch_buffer, 32);
            if (__builtin_expect(read_count == 0, 1)) return false;

            std::size_t i = 0;
            const std::size_t vectorized_chunks = read_count & ~std::size_t{3};

            for (; i < vectorized_chunks; i += 4) {
#if defined(__AVX2__)
                __m256i vec_data = _mm256_load_si256(reinterpret_cast<const __m256i*>(&batch_buffer[i]));
                demuxer.dispatch_simd(vec_data);
#else
                demuxer.process(batch_buffer[i + 0]);
                demuxer.process(batch_buffer[i + 1]);
                demuxer.process(batch_buffer[i + 2]);
                demuxer.process(batch_buffer[i + 3]);
#endif
            }

            for (; i < read_count; ++i) {
                demuxer.process(batch_buffer[i]);
            }
            return true;
        }

        SLAB_FORCE_INLINE bool push_from_producer(std::size_t producer_id, std::size_t routing_key, typename ConduitType::value_type token) noexcept {
            std::size_t target_consumer = routing_key & MASK;
            return mesh_[target_consumer][producer_id].try_push(token);
        }
    };

    // ============================================================================
    // 5. SHARDED MATRIX ROUTE
    // ============================================================================
    template <typename T, std::size_t IngressShards, std::size_t ExecutionWidth>
    class sharded_matrix_route {
    private:
        core::mpmc_matrix_conduit<T, 65536, IngressShards>& matrix_;
        std::size_t my_assigned_lane_id_;

    public:
        explicit sharded_matrix_route(
            core::mpmc_matrix_conduit<T, 65536, IngressShards>& shared_conduit,
            std::size_t lane_id
        ) noexcept : matrix_(shared_conduit), my_assigned_lane_id_(lane_id) {}

        SLAB_FORCE_INLINE std::size_t consume_batch_count(T* batch_buffer, std::size_t max_count) noexcept {
            return matrix_.pop_batch_lane(my_assigned_lane_id_, batch_buffer, max_count);
        }

        template <typename DemuxerType>
        SLAB_FORCE_INLINE bool consume_batch(DemuxerType& demuxer, T* __restrict__ batch_buffer) noexcept {
            std::size_t read_count = consume_batch_count(batch_buffer, 32);
            if (__builtin_expect(read_count == 0, 1)) return false;

            for (std::size_t i = 0; i < read_count; ++i) {
                demuxer.process(batch_buffer[i]);
            }
            return true;
        }

        template <typename DemuxerType>
        SLAB_FORCE_INLINE bool consume_batch_vector(DemuxerType& demuxer, T* __restrict__ batch_buffer) noexcept {
            std::size_t read_count = consume_batch_count(batch_buffer, 32);
            if (__builtin_expect(read_count == 0, 1)) return false;

            std::size_t i = 0;
            const std::size_t vectorized_chunks = read_count & ~std::size_t{3};

            for (; i < vectorized_chunks; i += 4) {
#if defined(__AVX2__)
                __m256i vec_data = _mm256_load_si256(reinterpret_cast<const __m256i*>(&batch_buffer[i]));
                demuxer.dispatch_simd(vec_data);
#else
                demuxer.process(batch_buffer[i + 0]);
                demuxer.process(batch_buffer[i + 1]);
                demuxer.process(batch_buffer[i + 2]);
                demuxer.process(batch_buffer[i + 3]);
#endif
            }

            for (; i < read_count; ++i) {
                demuxer.process(batch_buffer[i]);
            }
            return true;
        }
    };

} // namespace slabflux::core
