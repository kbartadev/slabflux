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
#include <bit>
#include <x86intrin.h>
#include <array>
#include <new>
#ifndef _WIN32
#include <liburing.h>
#include <sys/socket.h>
#include <atomic>
#include <span>
#include "../core.hpp"
#include "slabflux/platform/os.hpp"
#else
#include <atomic>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <system_error>
#include <type_traits>
#include "../core.hpp"
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#ifndef _WIN32
#include <sys/socket.h>
#include <cerrno>
using os_socket_t = int;
constexpr int OS_EWOULDBLOCK = EAGAIN;
#ifndef INVALID_SOCKET
constexpr int INVALID_SOCKET = -1;
#endif
#else
#include <winsock2.h>
using os_socket_t = SOCKET;
constexpr int OS_EWOULDBLOCK = WSAEWOULDBLOCK;
#endif

namespace slabflux::net {

    // ---------------------------------------------------------
    // 5. & 4. Stricter ConduitEvent Concept & Trivially Copyable
    // ---------------------------------------------------------
    template <typename T>
    concept ConduitEvent = requires {
        { T::TYPE_ID } -> std::convertible_to<uint32_t>;
    }&& std::is_trivially_copyable_v<T>;

    template <typename T>
    struct alignas(std::hardware_constructive_interference_size) wire_frame {
        uint32_t type_id;
        std::array<std::byte, sizeof(T)> payload;
    };

    /**
     * @brief Wire Protocol Decomposer.
     * @details Decouples wire framing from business logic using C++20 
     * structural bindings and std::bit_cast for bit-perfect translation.
     */
    template <typename T>
    struct wire_protocol {
        /** @brief Bit-perfect translation utilizing memory-safe copies to avoid stack blowups. */
        SLAB_FORCE_INLINE static void deserialize(const wire_frame<T>& frame, T& out_ev) noexcept {
            std::memcpy(&out_ev, frame.payload.data(), sizeof(T));
        }

        /** @brief Serializes a high-level event into a wire-ready frame. */
        SLAB_FORCE_INLINE static void serialize(wire_frame<T>& frame, const T& ev) noexcept {
            frame.type_id = T::TYPE_ID;
            std::memcpy(frame.payload.data(), &ev, sizeof(T));
        }
    };

    template <ConduitEvent T, size_t Capacity = 1024>
    class alignas(std::hardware_constructive_interference_size) network_conduit_socket {
    private:
        static_assert((Capacity& (Capacity - 1)) == 0, "Capacity must be a power of 2.");
        static constexpr size_t MASK = Capacity - 1;
        static constexpr size_t FRAME_SIZE = sizeof(wire_frame<T>);

        // 7. OS-specific invalid socket
        os_socket_t sock_fd_{ INVALID_SOCKET };

        // Atomic flag, since both push() (Producer) and poll_* (Consumer) read/write it
        alignas(std::hardware_constructive_interference_size) std::atomic<bool> is_dead_{ true };

        // ---------------------------------------------------------
        // TX Ring (Producer: Local Pipeline / Consumer: OS Socket)
        // ---------------------------------------------------------
        alignas(std::hardware_constructive_interference_size) std::unique_ptr<wire_frame<T>[]> tx_ring_{new wire_frame<T>[Capacity]};
        alignas(std::hardware_constructive_interference_size) std::atomic<size_t> tx_head_{ 0 };
        alignas(std::hardware_constructive_interference_size) std::atomic<size_t> tx_tail_{ 0 };

        // Ring-Buffer Instruction Interleaving & Shadow Caching
        alignas(std::hardware_constructive_interference_size) size_t cached_tx_tail_{ 0 }; // Producer-exclusive shadow register
        alignas(std::hardware_constructive_interference_size) size_t cached_tx_head_{ 0 }; // Consumer-exclusive shadow register

        // 1. Partial Send State (Only the poll thread reads/writes this; no atomics needed)
        size_t tx_inflight_offset_{ 0 };

        // ---------------------------------------------------------
        // RX State (Consumer: OS Socket / Producer: Local Domain)
        // ---------------------------------------------------------
        alignas(std::hardware_constructive_interference_size) std::unique_ptr<char[]> rx_buffer_{new char[FRAME_SIZE * 2]};
        size_t rx_cursor_{ 0 };

    public:
        network_conduit_socket() = default;

        void bind_socket(os_socket_t fd) noexcept {
            // Ideally assert that fd is actually in O_NONBLOCK mode
            sock_fd_ = fd;
            is_dead_.store(false, std::memory_order_release);
        }

        template <typename SmartPtr>
        [[nodiscard]] bool push(SmartPtr& ev) noexcept {
            static_assert(!std::is_pointer_v<SmartPtr>, "Raw pointers are strictly prohibited. Pass by reference or smart pointer.");
            if (SL_EXPECT_FALSE(is_dead_.load(std::memory_order_acquire) || !ev)) return false;
            return push_ref(*ev);
        }

        [[nodiscard]] bool push_ref(const T& ev) noexcept {
            if (SL_EXPECT_FALSE(is_dead_.load(std::memory_order_acquire))) return false;

            const size_t head = tx_head_.load(std::memory_order_relaxed);
            
            // Instruction Interleaving: Fast-path via core-local shadow register.
            // Eliminates L3 cache bouncing by avoiding the atomic read of tx_tail_ unless full.
            if (head - cached_tx_tail_ >= Capacity) {
                // Prefetch the atomic variable into L1 before the load stalls the pipeline
                _mm_prefetch(reinterpret_cast<const char*>(&tx_tail_), _MM_HINT_T0);
                cached_tx_tail_ = tx_tail_.load(std::memory_order_acquire);
                if (head - cached_tx_tail_ >= Capacity) return false;
            }

            // Software Pipelining: Issue hardware prefetch for the NEXT slot to 
            // hide memory latency while the CPU serializes into the CURRENT slot.
            _mm_prefetch(reinterpret_cast<const char*>(&tx_ring_[(head + 1) & MASK]), _MM_HINT_T0);

            wire_protocol<T>::serialize(tx_ring_[head & MASK], ev);
            asm volatile("" ::: "memory"); // Enforce payload retirement before head update
            tx_head_.store(head + 1, std::memory_order_release);
            return true;
        }

        // 2. Poll TX: Cyclic drain and partial-send handling
        template <typename DomainType>
        void poll_tx(DomainType& /*domain*/) noexcept {
            if (SL_EXPECT_FALSE(is_dead_.load(std::memory_order_relaxed))) return;

            size_t tail = tx_tail_.load(std::memory_order_relaxed);
            
            // Instruction Interleaving: Consumer shadow register.
            // We only incur cross-core coherence traffic if we've exhausted our known workload.
            if (tail == cached_tx_head_) {
                _mm_prefetch(reinterpret_cast<const char*>(&tx_head_), _MM_HINT_T0);
                cached_tx_head_ = tx_head_.load(std::memory_order_acquire);
            }
            
            const size_t head = cached_tx_head_;

            // TODO(Performance): Implement vector I/O (writev / WSASend) for true batching.
            // Currently relies on a tight non-blocking send() loop.
            while (tail < head) {
                // Software Pipelining: Fetch the next frame payload into L1 cache
                // to overlap with the socket send() execution.
                if (tail + 1 < head) [[likely]] _mm_prefetch(reinterpret_cast<const char*>(&tx_ring_[(tail + 1) & MASK]), _MM_HINT_T0);

                wire_frame<T>& frame = tx_ring_[tail & MASK];

                const char* data_ptr = reinterpret_cast<const char*>(&frame) + tx_inflight_offset_;
                const size_t bytes_to_send = FRAME_SIZE - tx_inflight_offset_;

                // Prevent SIGPIPE process termination on remote TCP RST
                int bytes_sent = ::send(sock_fd_, data_ptr, bytes_to_send, MSG_NOSIGNAL);

                if (bytes_sent > 0) {
                    tx_inflight_offset_ += bytes_sent;

                    if (tx_inflight_offset_ == FRAME_SIZE) {
                        // Full frame sent, move to next
                        tx_inflight_offset_ = 0;
                        tail++;
                    }
                }
                else {
                    int err = get_last_os_error();
#ifndef _WIN32
                    if (err == OS_EWOULDBLOCK || err == EINTR) {
#else
                    if (err == OS_EWOULDBLOCK || err == WSAEINTR) {
#endif
                        break;  // TCP window full, deterministic yield
                    }
                    else {
                        mark_dead();
                        break;
                    }
                }
            }
            tx_tail_.store(tail, std::memory_order_release);
        }

        // 3. Poll RX: Continuous drain until the kernel buffer is fully exhausted
        template <typename DomainType, typename SinkType>
        void poll_rx(DomainType& local_domain, SinkType& local_sink) noexcept {
            if (is_dead_.load(std::memory_order_relaxed)) return;

            while (true) {
                const size_t bytes_to_read = (FRAME_SIZE * 2) - rx_cursor_;
                if (bytes_to_read == 0) break; // Buffer is full, process what we have

                int bytes_read = ::recv(sock_fd_, rx_buffer_.get() + rx_cursor_, bytes_to_read, MSG_NOSIGNAL);

                if (bytes_read > 0) {
                    rx_cursor_ += bytes_read;

                    size_t frames_in_buffer = rx_cursor_ / FRAME_SIZE;
                    for (size_t i = 0; i < frames_in_buffer; ++i) {
                        wire_frame<T>* frame = reinterpret_cast<wire_frame<T>*>(rx_buffer_.get() + (i * FRAME_SIZE));
                        if (SL_EXPECT_FALSE(frame->type_id != T::TYPE_ID)) {
                            mark_dead();
                            return;
                        }

                        auto ev = local_domain.template make<T>();
                        if (ev) {
                            wire_protocol<T>::deserialize(*frame, *ev);
                            // 100% ZERO-OVERHEAD CALL: No virtual dispatch!
                            if constexpr (requires { local_sink.process(*ev); }) {
                                local_sink.process(*ev);
                            } else {
                                local_sink.on(*ev);
                            }
                        }
                    }

                    const size_t consumed_bytes = frames_in_buffer * FRAME_SIZE;
                    if (consumed_bytes > 0 && consumed_bytes < rx_cursor_) {
                        std::memmove(rx_buffer_.get(), rx_buffer_.get() + consumed_bytes, rx_cursor_ - consumed_bytes);
                    }
                    rx_cursor_ -= consumed_bytes;
                }
                else if (bytes_read == 0) {
                    mark_dead();
                    break;
                }
                else {
                    int err = get_last_os_error();
#ifndef _WIN32
                    if (err == OS_EWOULDBLOCK || err == EINTR) {
#else
                    if (err == OS_EWOULDBLOCK || err == WSAEINTR) {
#endif
                        break;
                    }
                    else {
                        mark_dead();
                        break;
                    }
                }
            }
        }

        [[nodiscard]] bool is_alive() const noexcept {
            return !is_dead_.load(std::memory_order_acquire);
        }

    private:
        void mark_dead() noexcept {
            bool expected = false;
            if (is_dead_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
                // PHYSICAL CLEANUP: Reset all partial-transfer state.
                // This prevents "Zombie Frame" corruption if the Orchestrator
                // later binds a new OS socket to this conduit.
                rx_cursor_ = 0;
                tx_inflight_offset_ = 0;

                // 6. DEAD Transition surfacing
                // TODO(Architecture): Emit an SLABFLUX system event (e.g., conduit_dead_event)
                // into the local runtime topology to trigger failover/orchestration routines.
            }
        }

        static int get_last_os_error() noexcept {
#if defined(_WIN32)
            return WSAGetLastError();
#else
            return errno;
#endif
        }
    };

#ifndef _WIN32
    /**
     * @class network_conduit_uring
     * @brief io_uring + SlabFlux Pool fusion.
     * @details O(1) complexity, 0ns syscall overhead.
     */
    template <ConduitEvent T, size_t Capacity>
    class alignas(std::hardware_constructive_interference_size) network_conduit_uring {
        static_assert((Capacity& (Capacity - 1)) == 0, "Capacity must be power of 2");
        static constexpr size_t MASK = Capacity - 1;

        struct io_uring ring_;
        int sock_fd_{ -1 };
        std::atomic<bool> is_dead_{ true };

        // TX Tracking: tracking "in-flight" packets residing in the kernel
        alignas(std::hardware_constructive_interference_size) std::array<T*, Capacity> tx_inflight_;
        
        // CRITICAL FIX: io_uring Out-Of-Order Array Overwrite (UAF / RCE)
        // Replaces sqe_tail index wrapping with an O(1) free-list to guarantee slot isolation.
        std::array<size_t, Capacity> free_indices_;
        size_t free_head_{0};
        
        std::atomic<size_t> inflight_count_{ 0 };

        // Inter-poll CQE Staging Buffer
        alignas(64) std::array<io_uring_cqe, Capacity> pending_rx_cqes_;
        size_t pending_rx_count_{0};

    public:
        explicit network_conduit_uring(int entries = Capacity) {
            for (size_t i = 0; i < Capacity; ++i) {
                tx_inflight_[i] = nullptr;
                free_indices_[i] = i;
            }
            io_uring_params params{};
            // enforce SQPOLL for zero-syscall operation
            params.flags |= IORING_SETUP_SQPOLL | IORING_SETUP_SQ_AFF;
            params.sq_thread_cpu = 3; // Hardware-specific: pin the kernel poller thread

            if (io_uring_queue_init_params(entries, &ring_, &params) < 0) {
                throw std::runtime_error("Kernel bypass initialization failed");
            }
        }

        ~network_conduit_uring() {
            if (sock_fd_ != -1) {
                ::io_uring_unregister_files(&ring_);
                ::close(sock_fd_);
            }
            io_uring_queue_exit(&ring_);
        }

        void bind_socket(int fd) noexcept {
            sock_fd_ = fd;
            
            // Fixed File Registration: Bypasses kernel fget/fput atomic reference counting.
            // This is an optimization for high-throughput zero-copy bypassing.
            ::io_uring_register_files(&ring_, &sock_fd_, 1);
            is_dead_.store(false, std::memory_order_release);
        }

        /**
         * @brief Fire an event to the NIC.
         * @details No waiting, no copying. Ownership transfers to the kernel.
         */
        [[nodiscard]] bool push(T* ev) noexcept {
            if (SL_EXPECT_FALSE(is_dead_.load(std::memory_order_acquire) || !ev)) return false;

            if (inflight_count_.load(std::memory_order_acquire) >= Capacity) [[unlikely]] return false;

            io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
            if (!sqe) [[unlikely]] return false; // Strict backpressure

            // Take ownership and store it in the in-flight list
            size_t idx = free_indices_[free_head_++];
            tx_inflight_[idx] = ev;
            inflight_count_.fetch_add(1, std::memory_order_release);

            // Prepare SEND operation using Direct Descriptors (Index 0 instead of fd)
            // Forces the kernel to bypass file table lock contention
            io_uring_prep_send(sqe, 0, ev, sizeof(T), MSG_NOSIGNAL);
            sqe->flags |= IOSQE_FIXED_FILE;

            // CRITICAL FIX: Cryptographically tag the user_data to separate TX from RX!
            io_uring_sqe_set_data64(sqe, idx | (1ULL << 63));

            // Under SQPOLL this is not a syscall, just a memory barrier and pointer bump
            io_uring_submit(&ring_);
            return true;
        }

        template <typename DomainType>
        void poll_tx(DomainType& domain) noexcept {
            if (is_dead_.load(std::memory_order_relaxed)) return;
            drain_tx_cqes(domain);
        }

        template <typename DomainType, typename SinkType>
        void poll_rx(DomainType& domain, SinkType& sink) noexcept {
            if (is_dead_.load(std::memory_order_relaxed)) return;
            
            io_uring_cqe* cqe;
            unsigned head;
            int count = 0;

            auto process_rx_cqe = [&](const io_uring_cqe& cqe_val) {
                T* raw_ptr = reinterpret_cast<T*>(cqe_val.user_data);
                auto deleter = [](void* ctx, void* ptr) noexcept {
                    static_cast<DomainType*>(ctx)->release(static_cast<T*>(ptr));
                };
                core::scoped_ptr<T> ev(raw_ptr, deleter, &domain);

                if (cqe_val.res > 0 && static_cast<size_t>(cqe_val.res) == sizeof(T)) {
                    if constexpr (requires { sink.process(*ev); }) { sink.process(*ev); } 
                    else { sink.on(*ev); }
                } else if (cqe_val.res <= 0 && cqe_val.res != -EAGAIN && cqe_val.res != -EINTR) {
                    mark_dead(domain);
                }
            };

            // Drain stashed RX completions intercepted by poll_tx
            for (size_t i = 0; i < pending_rx_count_; ++i) process_rx_cqe(pending_rx_cqes_[i]);
            pending_rx_count_ = 0;

            // Drain completed asynchronous receives
            io_uring_for_each_cqe(&ring_, head, cqe) {
                uint64_t ud = cqe->user_data;
                if (ud & (1ULL << 63)) {
                    size_t idx = ud & ~(1ULL << 63);
                    if (tx_inflight_[idx]) {
                        // Hardware Memory Sanitization
                        const __m512i zero = _mm512_setzero_si512();
                        size_t offset = 0;
                        for (; offset + 64 <= sizeof(T); offset += 64) _mm512_storeu_si512(reinterpret_cast<__m512i*>(reinterpret_cast<char*>(tx_inflight_[idx]) + offset), zero);
                        if (offset < sizeof(T)) std::memset(reinterpret_cast<char*>(tx_inflight_[idx]) + offset, 0, sizeof(T) - offset);
                        
                        domain.release(tx_inflight_[idx]);
                        tx_inflight_[idx] = nullptr;
                        free_indices_[--free_head_] = idx;
                        inflight_count_.fetch_sub(1, std::memory_order_release);
                    }
                } else {
                    process_rx_cqe(*cqe);
                }
                count++;
            }
            if (count > 0) io_uring_cq_advance(&ring_, count);

            refill_rx(domain, sink);
        }

    private:
        template <typename DomainType>
        void drain_tx_cqes(DomainType& domain) noexcept {
            io_uring_cqe* cqe;
            unsigned head;
            int count = 0;

            io_uring_for_each_cqe(&ring_, head, cqe) {
                uint64_t ud = cqe->user_data;
                if (ud & (1ULL << 63)) {
                    size_t idx = ud & ~(1ULL << 63);
                    if (tx_inflight_[idx]) {
                        domain.release(tx_inflight_[idx]);
                        tx_inflight_[idx] = nullptr;
                        free_indices_[--free_head_] = idx;
                        inflight_count_.fetch_sub(1, std::memory_order_release);
                    }
                } else {
                    if (pending_rx_count_ < Capacity) {
                        pending_rx_cqes_[pending_rx_count_++] = *cqe;
                    }
                }
                count++;
            }
            io_uring_cq_advance(&ring_, count);
        }

        void refill_rx(auto& domain, auto& /*sink*/) noexcept {
            // Zero-copy receive: directly into memory obtained from the pool
            while (true) {
                auto ev = domain.template make<T>();
                if (!ev) break;

                io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
                if (!sqe)
                {
                    // Scope exit: RAII automatically reclaims the memory back to the pool
                    break;
                }

                io_uring_prep_recv(sqe, 0, ev.get(), sizeof(T), MSG_WAITALL);
                sqe->flags |= IOSQE_FIXED_FILE;
                io_uring_sqe_set_data(sqe, ev.release()); // Hand ownership to the kernel to prevent Use-After-Free
                io_uring_submit(&ring_);
            }
        }

        template <typename DomainType>
        void mark_dead(DomainType& domain) noexcept {
            bool expected = false;
            if (is_dead_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
                for (auto& ev : tx_inflight_) {
                    if (ev) {
                        domain.release(ev);
                        ev = nullptr;
                    }
                }
            }
        }
    };

    template <ConduitEvent T, size_t Capacity = 1024>
    using network_conduit = network_conduit_uring<T, Capacity>;
#else
    template <ConduitEvent T, size_t Capacity = 1024>
    using network_conduit = network_conduit_socket<T, Capacity>;
#endif
}  // namespace slabflux::net
