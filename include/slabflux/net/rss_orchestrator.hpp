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
 * ============================================================================* @file rss_orchestrator.hpp
 * @brief DPDK Receive Side Scaling (RSS) Orchestrator for linear horizontal scaling.
 */

#pragma once

#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_launch.h>
#include <rte_cycles.h>
#include <pthread.h>
#include <signal.h>
#include <setjmp.h>
#include <iostream>
#include <vector>
#include <stdexcept>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/net/matrix_nexus.hpp"
#include "slabflux/core/thread_context.hpp"

namespace slabflux::net::detail {
    // Thread-local recovery jump buffer for terminal core hangs
    inline thread_local sigjmp_buf hung_core_jmp_buf;
    inline thread_local bool hung_core_jmp_valid = false;

    inline void handle_core_hang_signal(int) {
        if (hung_core_jmp_valid) siglongjmp(hung_core_jmp_buf, 1);
    }
}

namespace slabflux::net {

    template <typename Gateway, typename EgressConduit>
    class alignas(core::CACHE_LINE_SIZE) rss_orchestrator {
        
        // Padded to 64-bytes to completely prevent False Sharing across L1 caches
        // when orchestrator updates the `running` control flag.
        struct alignas(core::CACHE_LINE_SIZE) worker_context {
            matrix_nexus<Gateway, EgressConduit>* nexus{nullptr};
            volatile bool running{true};
            uint32_t lcore_id{RTE_MAX_LCORE};
            pthread_t thread_id{0};
        };

        std::vector<worker_context> workers_;
        uint16_t port_id_;

        static int launch_worker(void* arg) {
            auto* ctx = static_cast<worker_context*>(arg);
            
            ctx->thread_id = pthread_self();
            
            // Initialize the thread-local worker ID using DPDK's assigned lcore
            core::thread_context::worker_id = rte_lcore_id();
            
            // Install signal handler for forceful recovery from terminal hangs
            struct sigaction sa{};
            sa.sa_handler = detail::handle_core_hang_signal;
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = 0;
            sigaction(SIGUSR1, &sa, nullptr);

            // DPDK masks most signals on worker lcores by default. 
            // We MUST explicitly unblock SIGUSR1 so the Watchdog can forcefully reclaim the core.
            sigset_t set;
            sigemptyset(&set);
            sigaddset(&set, SIGUSR1);
            pthread_sigmask(SIG_UNBLOCK, &set, nullptr);

            if (sigsetjmp(detail::hung_core_jmp_buf, 1) == 0) {
                detail::hung_core_jmp_valid = true;
                while (ctx->running) {
                    ctx->nexus->poll_and_execute();
                }
            } else {
                std::cerr << "[SlabFlux] Lcore " << rte_lcore_id() << " forcibly reclaimed from hung state.\n";
            }
            detail::hung_core_jmp_valid = false;
            return 0;
        }

    public:
        rss_orchestrator(uint16_t port_id) : port_id_(port_id) {}

        /**
         * @brief Configures the physical NIC with TCP 4-Tuple RSS.
         * @details Enforces mathematical affinity: all packets for a single TCP connection
         * will hash to the exact same RX queue, guaranteeing lock-free, single-core isolation.
         */
        static void configure_hardware_rss(uint16_t port_id, uint16_t num_queues, struct rte_mempool* mempool) {
            struct rte_eth_conf port_conf = {};
            port_conf.rxmode.mq_mode = RTE_ETH_MQ_RX_RSS;
            
            // Hardware Hash constraints: Enforce 4-Tuple hashing for symmetric TCP flows
            port_conf.rx_adv_conf.rss_conf.rss_hf = RTE_ETH_RSS_NONFRAG_IPV4_TCP | RTE_ETH_RSS_IPV4;
            port_conf.txmode.mq_mode = RTE_ETH_MQ_TX_NONE;

            if (rte_eth_dev_configure(port_id, num_queues, num_queues, &port_conf) < 0) {
                throw std::runtime_error("Failed to configure DPDK ethdev with RSS.");
            }

            uint16_t nb_rx_desc = 1024;
            uint16_t nb_tx_desc = 1024;
            rte_eth_dev_adjust_nb_rx_tx_desc(port_id, &nb_rx_desc, &nb_tx_desc);

            for (uint16_t q = 0; q < num_queues; ++q) {
                if (rte_eth_rx_queue_setup(port_id, q, nb_rx_desc, rte_eth_dev_socket_id(port_id), nullptr, mempool) < 0) {
                    throw std::runtime_error("Failed to setup RX queue.");
                }
                if (rte_eth_tx_queue_setup(port_id, q, nb_tx_desc, rte_eth_dev_socket_id(port_id), nullptr) < 0) {
                    throw std::runtime_error("Failed to setup TX queue.");
                }
            }

            if (rte_eth_dev_start(port_id) < 0) {
                throw std::runtime_error("Failed to start DPDK ethdev.");
            }
            
            // Enable promiscuous mode to catch all MACs targeting this Edge Node
            rte_eth_promiscuous_enable(port_id);
        }

        /**
         * @brief Orchestrates and pins the Nexus loops to isolated physical cores.
         */
        void ignite(const std::vector<matrix_nexus<Gateway, EgressConduit>*>& nexuses) {
            workers_.resize(nexuses.size());
            
            uint32_t lcore_id = rte_get_next_lcore(-1, 1, 0); // Find first available secondary core
            
            for (size_t i = 0; i < nexuses.size(); ++i) {
                if (lcore_id == RTE_MAX_LCORE) throw std::runtime_error("Insufficient DPDK lcores to map to all RSS queues.");
                workers_[i].nexus = nexuses[i];
                workers_[i].running = true;
                workers_[i].lcore_id = lcore_id;
                rte_eal_remote_launch(launch_worker, &workers_[i], lcore_id); // Pin to silicon!
                lcore_id = rte_get_next_lcore(lcore_id, 1, 0);
            }
        }

        /**
         * @brief Polls DPDK hardware telemetry natively.
         * @details Retrieves precise RX/TX drops and throughput stats from the NIC silicon.
         */
        void print_telemetry() const {
            struct rte_eth_stats stats;
            if (rte_eth_stats_get(port_id_, &stats) == 0) {
                std::cout << "[DPDK Telemetry] Port " << port_id_ << "\n"
                          << "  RX Packets: " << stats.ipackets << "\n"
                          << "  TX Packets: " << stats.opackets << "\n"
                          << "  RX Missed:  " << stats.imissed << " (Hardware queue full)\n"
                          << "  RX Errors:  " << stats.ierrors << " (Malformed frames)\n"
                          << "  TX Errors:  " << stats.oerrors << " (Hardware TX drops)\n";
            }
        }

        /**
         * @brief Exposes the raw TSC heartbeat from the pinned nexus loop.
         * @details Allows high-fidelity Watchdog testing to monitor core determinism natively.
         */
        uint64_t get_worker_heartbeat(size_t worker_index) const noexcept {
            if (worker_index < workers_.size() && workers_[worker_index].nexus) {
                return workers_[worker_index].nexus->get_heartbeat();
            }
            return 0;
        }

        void halt(uint32_t timeout_ms = 5000) {
            for (auto& w : workers_) w.running = false;
            
            uint64_t start_tsc = rte_rdtsc();
            uint64_t timeout_tsc = (rte_get_timer_hz() / 1000) * timeout_ms;

            for (auto& w : workers_) {
                if (w.lcore_id != RTE_MAX_LCORE) {
                    while (rte_eal_get_lcore_state(w.lcore_id) == RUNNING) {
                        if (rte_rdtsc() - start_tsc > timeout_tsc) {
                            std::cerr << "[SlabFlux] Lcore " << w.lcore_id << " failed to halt (Hung Core). Forcefully reclaiming via signal.\n";
                            if (w.thread_id != 0) {
                                pthread_kill(w.thread_id, SIGUSR1);
                                rte_delay_ms(50); // Allow thread to gracefully return to FINISHED state
                            }
                            break;
                        }
                        rte_pause();
                    }
                }
            }
        }
    };
} // namespace slabflux::net