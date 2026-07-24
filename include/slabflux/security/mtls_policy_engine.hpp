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

#include <cstdint>
#include <atomic>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::security {

    /**
     * @brief Static Policy Engine for mTLS Enforcement.
     * @details Manages subnet-level rules for forcing mutual TLS on 
     * high-security ingress paths.
     */
    class mtls_policy_engine {
        struct alignas(16) subnet_rule {
            uint32_t network;
            uint32_t mask;
            bool active;
        };

        static constexpr size_t MAX_RULES = 128;
        static inline subnet_rule rules_[MAX_RULES]{};
        static inline std::atomic<uint32_t> rule_count_{0};

    public:
        /**
         * @brief Adds a network/mask pair to the mandatory mTLS enforcement list.
         */
        static void enforce_for_subnet(uint32_t network_ip, uint32_t mask) noexcept {
            uint32_t idx = rule_count_.fetch_add(1, std::memory_order_relaxed);
            if (SL_EXPECT_FALSE(idx >= MAX_RULES)) return;

            rules_[idx] = { network_ip, mask, true };
        }

        /**
         * @brief Evaluates if a given source IP requires a client certificate.
         */
        SLAB_HOT static bool is_mtls_required(uint32_t src_ip) noexcept {
            const uint32_t current_count = rule_count_.load(std::memory_order_acquire);
            
            for (uint32_t i = 0; i < current_count; ++i) {
                if ((src_ip & rules_[i].mask) == rules_[i].network) {
                    return true;
                }
            }
            return false;
        }
    };

} // namespace slabflux::security