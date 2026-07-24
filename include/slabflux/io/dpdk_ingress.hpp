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
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <immintrin.h> // For rte_prefetch0
#include <rte_prefetch.h>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::io {

    template <typename EventType, size_t Capacity, typename PipelineLogic>
    class alignas(64) dpdk_ingress {
        PipelineLogic& logic_;
        uint16_t port_id_;
        uint16_t queue_id_;
        alignas(64) uint64_t full_drop_count_{0};

    public:
        dpdk_ingress(PipelineLogic& logic, uint16_t port, uint16_t queue)
            : logic_(logic), port_id_(port), queue_id_(queue) {}

        /**
         * @brief Zero-Syscall, Zero-Interrupt Hardware Polling.
         * @details Directly scans the NIC's DMA memory region over PCIe.
         */
        SLAB_HOT void poll() noexcept {
            rte_mbuf* rx_pkts[32];
            
            // 1. Hardware Fetch: Reads NIC registers mapped in User-Space
            const uint16_t nb_rx = rte_eth_rx_burst(port_id_, queue_id_, rx_pkts, 32);
            if (SL_EXPECT_TRUE(nb_rx == 0)) return;

            // 2. Hardware Prefetch Pipeline: Preload network payloads into L1-D Cache
            for (uint16_t i = 0; i < nb_rx; ++i) {
                rte_prefetch0(rte_pktmbuf_mtod(rx_pkts[i], void*));
            }

            // 3. Deterministic Dispatch
            for (uint16_t i = 0; i < nb_rx; ++i) {
                rte_mbuf* mbuf = rx_pkts[i];
                
                // Zero-copy resolution: We parse data exactly where the NIC placed it
                EventType* ev = rte_pktmbuf_mtod(mbuf, EventType*);
                
                if (SL_EXPECT_FALSE(!logic_.on_raw_frame(ev, rte_pktmbuf_data_len(mbuf)))) {
                    full_drop_count_++;
                }
                
                // 4. Hardware Recycle: Tell the NIC the buffer is free
                rte_pktmbuf_free(mbuf);
            }
        }
    };
} // namespace slabflux::io
