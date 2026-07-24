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
#include <vector>
#include <cstddef>
#include <thread>
#include <functional>
#include <utility>
#include <tuple>
#include <immintrin.h>
#include "slabflux/core/alignment_checks.hpp"
#include "slabflux/hw/intrinsics.hpp" // For hw::tzcnt_64
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/hardware_topology.hpp"
#include "slabflux/core/spsc_conduit.hpp"
#include "slabflux/core/sf_node_ctx.hpp"
#include "slabflux/rte/environment.hpp"
#include "slabflux/hw/spin_backoff.hpp"

namespace slabflux::core {

    /**
     * @brief Zero-Allocation Fixed-Size Task Wrapper.
     * @details Replaces std::packaged_task to achieve zero heap allocation.
     */
    class task {
        alignas(64) char storage_[128];
        bool (*invoker_)(void*) = nullptr;
        void (*deleter_)(void*) = nullptr;
        static constexpr uint8_t STATE_READY = 0x1;
        static constexpr uint8_t STATE_RESULT = 0x2;
        std::atomic<uint8_t> state_{0};

    public:
        template <typename F>
        void assign(F&& func) {
            // Lifecycle Guard: Clean up previous capture to prevent memory leaks
            if (deleter_) {
                deleter_(storage_);
                deleter_ = nullptr;
            }

            using DecayF = std::decay_t<F>;
            static_assert(sizeof(DecayF) <= sizeof(storage_), "Task capture too large for task");
            
            new (storage_) DecayF(std::forward<F>(func));
            invoker_ = [](void* data) -> bool {
                return (*reinterpret_cast<DecayF*>(data))();
            };
            deleter_ = [](void* data) {
                reinterpret_cast<DecayF*>(data)->~DecayF();
            };
            state_.store(0, std::memory_order_release);
        }

        SLAB_HOT void operator()(bool execute = true) noexcept {
            bool result = false;
            if (SL_EXPECT_TRUE(execute)) {
                result = invoker_(storage_);
            }
            uint8_t final_state = STATE_READY | (result ? STATE_RESULT : 0);
            state_.store(final_state, std::memory_order_release);
        }

        SLAB_FORCE_INLINE bool poll_result(bool& out_result) const noexcept {
            uint8_t s = state_.load(std::memory_order_acquire);
            if (!(s & STATE_READY)) return false;
            out_result = (s & STATE_RESULT);
            return true;
        }

        ~task() {
            if (deleter_) {
                deleter_(storage_);
            }
        }
    };

    /**
     * @brief Zero-Allocation Future.
     * @details Replaces std::future to avoid heap allocations while 
     * maintaining blocking get/wait semantics for testing and synchronization.
     */
    class zero_alloc_future {
        task* task_{nullptr};
    public:
        zero_alloc_future() = default;
        explicit zero_alloc_future(task* t) noexcept : task_(t) {}
        
        zero_alloc_future(zero_alloc_future&& other) noexcept : task_(other.task_) {
            other.task_ = nullptr;
        }
        
        zero_alloc_future& operator=(zero_alloc_future&& other) noexcept {
            if (this != &other) {
                task_ = other.task_;
                other.task_ = nullptr;
            }
            return *this;
        }
        
        zero_alloc_future(const zero_alloc_future&) = delete;
        zero_alloc_future& operator=(const zero_alloc_future&) = delete;

        [[nodiscard]] bool valid() const noexcept { return task_ != nullptr; }

        bool get() const {
            if (!task_) return false;
            bool result = false;
            while (!task_->poll_result(result)) {
                _mm_pause();
            }
            return result;
        }

        void wait() const {
            if (!task_) return;
            bool dummy;
            while (!task_->poll_result(dummy)) {
                _mm_pause();
            }
        }
    };

    struct submission_result {
        zero_alloc_future future;
        task* task_ptr;

        template<std::size_t I> 
        auto& get() { 
            if constexpr (I == 0) return future; 
            else return task_ptr; 
        }
    };

    /**
     * @brief Task Matrix (Zero-Allocation).
     */
    template<size_t Capacity = 4096>
    class local_task_pool {
    public:
        struct alignas(64) task_node {
            task task_instance;
            std::atomic<bool> valid{true};
        };
        
        #if defined(__GNUC__) || defined(__clang__)
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Winvalid-offsetof"
        #endif
        SLAB_ENFORCE_OFFSET(task_instance, 0, task_node);
        #if defined(__GNUC__) || defined(__clang__)
        #pragma GCC diagnostic pop
        #endif

        static_assert(sizeof(task_node) % 64 == 0, "TaskNode breach: potential false sharing in Task Matrix.");

    private:
        task_node storage_[Capacity];
        std::atomic<uint32_t> cursor_{ 0 };
        
        // Presence Filter: 1 bit per task slot to track in-flight status.
        alignas(64) std::atomic<uint64_t> occupancy_filter_[Capacity / 64];

    public:
        local_task_pool() { 
            static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
            for (size_t i = 0; i < Capacity / 64; ++i) {
                occupancy_filter_[i].store(0, std::memory_order_relaxed);
            }
        }

        SLAB_HOT void mark_active(uint32_t idx) noexcept {
            occupancy_filter_[idx >> 6].fetch_or(1ULL << (idx & 63), std::memory_order_relaxed);
        }

        SLAB_HOT void mark_finished(task* task_ptr) noexcept {
            uint32_t idx = static_cast<uint32_t>(reinterpret_cast<task_node*>(task_ptr) - storage_);
            occupancy_filter_[idx >> 6].fetch_and(~(1ULL << (idx & 63)), std::memory_order_relaxed);
        }

        SLAB_HOT void detect_wrap_simd(uint32_t idx) const noexcept {
            const uint32_t qword_idx = (idx >> 6) % (Capacity / 64);
            __m512i v_occupancy = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(&occupancy_filter_[qword_idx & ~7]));
            if (SL_EXPECT_FALSE(_mm512_cmpeq_epi64_mask(v_occupancy, _mm512_setzero_si512()) != 0xFF)) {
                if (occupancy_filter_[idx >> 6].load(std::memory_order_relaxed) & (1ULL << (idx & 63))) {
                    handle_critical_error("LocalTaskPool Wrap-Around: Overwriting unconsumed validation task.");
                }
            }
        }

        template<typename F>
        task_node* acquire(F&& func) {
            uint32_t idx = cursor_.fetch_add(1, std::memory_order_relaxed) & (Capacity - 1);
            detect_wrap_simd(idx);
            mark_active(idx);
            storage_[idx].valid.store(true, std::memory_order_relaxed);
            storage_[idx].task_instance.assign(std::forward<F>(func));
            return &storage_[idx];
        }
    };

} // namespace slabflux::core

namespace std {
    template<> struct tuple_size<slabflux::core::submission_result> : integral_constant<std::size_t, 2> {};
    template<std::size_t I> struct tuple_element<I, slabflux::core::submission_result> {
        using type = std::conditional_t<I == 0, slabflux::core::zero_alloc_future, slabflux::core::task*>;
    };
}

namespace slabflux::core {

    class validation_pool {
        using Task = task;
        static constexpr size_t MAX_WORKERS = 64;

        std::thread workers_[MAX_WORKERS];
        alignas(64) spsc_conduit<Task*, 1024> worker_queues_[MAX_WORKERS];
        
        // Occupancy Mask: Each bit i represents worker i's "available" status.
        // 1 = Worker has capacity, 0 = Worker queue likely full.
        alignas(64) std::atomic<uint64_t> occupancy_mask_{ 0xFFFFFFFFFFFFFFFF };

        std::atomic<uint32_t> next_worker_{ 0 };
        std::atomic<bool> running_{ true };
        sf_node_ctx* context_{ nullptr };
        const size_t num_workers_;
        local_task_pool<4096> task_pool_;

    public:
        explicit validation_pool(size_t num_threads, int base_cpu_id, sf_node_ctx* ctx = nullptr) 
            : context_(ctx), num_workers_(std::min(num_threads, MAX_WORKERS)) {
            
            // Safe Mask Initialization: Handles the 64-worker case without UB bit-shift overflow.
            const uint64_t initial_mask = (num_workers_ == 64) ? ~0ULL : (1ULL << num_workers_) - 1;
            occupancy_mask_.store(initial_mask, std::memory_order_release);

            for (size_t i = 0; i < num_workers_; ++i) {
                workers_[i] = std::thread([this, i, base_cpu_id]() {
                    if (base_cpu_id >= 0) {
                        if (context_) slabflux::rte::watcher_on_thread_ignition(*context_, 4 + i); // Worker 4+: Validation Pool
                        hardware_topology::pin_thread(base_cpu_id + static_cast<int>(i));
                    }
                    
                    Task* t = nullptr;
                    uint32_t yield_count = 0;
                    while (running_.load(std::memory_order_relaxed)) {
                        if (worker_queues_[i].try_pop(t)) {
                            auto* node = reinterpret_cast<local_task_pool<4096>::task_node*>(t);
                            // Always invoke the task wrapper to satisfy the promise, 
                            // but pass the validity flag to determine if the logic should run.
                            const bool valid = node->valid.load(std::memory_order_acquire);
                            (*t)(valid);
                            
                            task_pool_.mark_finished(t);
                            
                            // Update bitset: Worker i is definitely not full now
                            occupancy_mask_.fetch_or(1ULL << i, std::memory_order_relaxed);
                            yield_count = 0;
                        } else {
                            slabflux::hw::spin_backoff(yield_count);
                        }
                    }
                });
            }
        }

        /**
         * @brief Submits a validation task to the pool.
         * @return A submission_result struct (passed via RAX/RDX) for structured binding support.
         */
        template<typename F>
        submission_result submit(F&& func) {
            auto* node = task_pool_.acquire(std::forward<F>(func));
            
            // Optimization: Atomically claim a worker slot via CAS loop.
            // This prevents race conditions where multiple producers target the same worker.
            uint64_t mask = occupancy_mask_.load(std::memory_order_relaxed);
            uint32_t worker_idx;

            for (uint32_t retries = 0; ; ++retries) {
                if (SL_EXPECT_TRUE(mask != 0)) {
                    worker_idx = static_cast<uint32_t>(hw::tzcnt_64(mask));
                    uint64_t bit = 1ULL << worker_idx;
                    if (occupancy_mask_.compare_exchange_strong(mask, mask ^ bit, std::memory_order_acq_rel)) break;
                } else {
                    // Saturated Fallback: Round-robin
                    worker_idx = next_worker_.fetch_add(1, std::memory_order_relaxed) % num_workers_;
                    break;
                }
                for (uint32_t k = 0; k < (1U << (retries & 3)); ++k) _mm_pause();
            }

            uint32_t push_yield = 0;
            while (!worker_queues_[worker_idx].try_push(&node->task_instance)) {
                worker_idx = (worker_idx + 1) % num_workers_;
                slabflux::hw::spin_backoff(push_yield);
            }
            
            return { zero_alloc_future(&node->task_instance), &node->task_instance };
        }

        /**
         * @brief Cancels a task if it hasn't been started yet.
         */
        void invalidate(Task* task_ptr) noexcept {
            if (!task_ptr) return;
            auto* node = reinterpret_cast<local_task_pool<4096>::task_node*>(task_ptr);
            node->valid.store(false, std::memory_order_release);
        }

        ~validation_pool() {
            running_.store(false, std::memory_order_relaxed);
            for (auto& w : workers_) {
                if (w.joinable()) w.join();
            }
        }
    };

} // namespace slabflux::core