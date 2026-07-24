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
#include <iostream>
#include <thread>
#include <tuple>

// OS-specific headers for Thread Affinity (CPU Pinning)
#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#endif

#include "slabflux/core.hpp"

namespace slabflux::runtime {

// ========================================================================
// 1. HARDWARE ISOLATION: CPU Pinning
// ========================================================================
inline bool pin_thread_to_core(int core_id) noexcept {
#if defined(_WIN32)
    DWORD_PTR mask = (static_cast<DWORD_PTR>(1) << core_id);
    if (SetThreadAffinityMask(GetCurrentThread(), mask) == 0) {
        return false;
    }
    // Request highest scheduler priority
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    return true;
#else
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_t current_thread = pthread_self();
    if (pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset) != 0) {
        return false;
    }
    return true;
#endif
}

// ========================================================================
// 2. TOPOLOGICAL BINDING
// ========================================================================
/// @brief Compile-time binding between a physical Conduit and a logical Sink.
template <typename ConduitType, typename SinkType>
struct conduit_binding {
    ConduitType& conduit;
    SinkType& sink;

    // Orchestrator callback: what to do when the network link dies?
    void (*on_dead)(ConduitType&) = nullptr;
};

template <typename C, typename S>
conduit_binding<C, S> bind_conduit(C& c, S& s) {
    return conduit_binding<C, S>{c, s};
}

// ========================================================================
// 3. SLABFLUX NODE RUNTIME (The Execution Engine)
// ========================================================================
template <typename DomainType, typename... Bindings>
class alignas(64) node_runtime {
   private:
    DomainType& domain_;
    std::tuple<Bindings...> bindings_;
    alignas(64) std::atomic<bool> is_running_{false};

   public:
    /// @brief Initializes the runtime with the memory domain and static topology.
    node_runtime(DomainType& domain, Bindings... binds)
        : domain_(domain), bindings_(std::make_tuple(binds...)) {}

    /// @brief Starts the deterministic event loop.
    /// @param core_id Physical CPU core ID (e.g., 2). If -1, no isolation.
    void run(int core_id = -1) noexcept {
        if (core_id >= 0) {
            if (!pin_thread_to_core(core_id)) {
                std::cerr << "[SLABFLUX Warn] Failed to pin thread to core " << core_id << "\n";
            }
        }

        is_running_.store(true, std::memory_order_release);

        // THE TIGHT POLL LOOP (No OS-level blocking)
        while (is_running_.load(std::memory_order_acquire)) {
            // std::apply + fold expression expands the tuple at compile-time.
            // No for-loop, no iterators — just raw assembly instructions in sequence.
            std::apply([this](auto&... binding) { (..., this->poll_single(binding)); }, bindings_);
        }
    }

    /// @brief Graceful shutdown (Safe termination of the Node)
    void stop() noexcept { is_running_.store(false, std::memory_order_release); }

   private:
    // O(1) fully inlined call for each binding
    template <typename Binding>
    __forceinline void poll_single(Binding& b) noexcept {
        if (b.conduit.is_alive()) {
            // 1. Drain network kernel buffer -> Local Domain (Sink)
            b.conduit.poll_rx(domain_, b.sink);

            // 2. Drain Local Domain TX Ring -> Network kernel buffer
            b.conduit.poll_tx();
        } else {
            // Orchestration: notify upper layers that the wire is dead
            if (b.on_dead != nullptr) {
                b.on_dead(b.conduit);
                b.on_dead = nullptr;  // Invoke only once
            }
        }
    }
};

}  // namespace slabflux::runtime
