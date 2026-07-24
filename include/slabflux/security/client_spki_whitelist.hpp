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
 * ============================================================================* SLABFLUX SOFTWARE ENGINE
 * Copyright (c) 2026 Kristóf Barta (https://github.com/kbartadev)
 * ============================================================================
 */

#pragma once

#include <cstdint>
#include <string_view>
#include <array>
#include <atomic>
#include <cstring>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::security {

    /**
     * @brief Zero-allocation SPKI (Subject Public Key Info) Whitelist.
     * @details Stores binary hashes of authorized client public keys. 
     * Optimized for hardware-accelerated comparison during the TLS handshake.
     */
    class client_spki_whitelist {
        static constexpr size_t MAX_WHITELIST_ENTRIES = 512;
        static constexpr size_t MAX_KEY_LEN = 64; // Supports SHA-512 or large ECDSA keys

        struct alignas(64) whitelist_entry {
            uint8_t raw_key[MAX_KEY_LEN];
            uint32_t len;
            bool active;
        };

        static inline whitelist_entry entries_[MAX_WHITELIST_ENTRIES]{};
        static inline std::atomic<uint32_t> entry_count_{0};

    public:
        /**
         * @brief Registers a binary SPKI string as authorized.
         */
        static void authorize(std::string_view spki_binary) noexcept {
            uint32_t idx = entry_count_.fetch_add(1, std::memory_order_relaxed);
            if (SL_EXPECT_FALSE(idx >= MAX_WHITELIST_ENTRIES)) return;

            auto& entry = entries_[idx];
            entry.len = static_cast<uint32_t>(std::min(spki_binary.size(), MAX_KEY_LEN));
            std::memcpy(entry.raw_key, spki_binary.data(), entry.len);
            entry.active = true;
        }

        /**
         * @brief Verifies if the provided key hash exists in the ignition whitelist.
         * @details Linear scan utilizing the hardware prefetcher.
         */
        SLAB_HOT static bool is_authorized(std::string_view spki_binary) noexcept {
            const uint32_t current_count = entry_count_.load(std::memory_order_acquire);
            
            for (uint32_t i = 0; i < current_count; ++i) {
                const auto& entry = entries_[i];
                // Structural comparison avoiding branches via short-circuiting
                if (entry.len == spki_binary.size() && 
                    std::memcmp(entry.raw_key, spki_binary.data(), entry.len) == 0) {
                    return true;
                }
            }
            return false;
        }
    };

} // namespace slabflux::security