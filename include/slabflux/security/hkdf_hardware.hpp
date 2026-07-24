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
 * ============================================================================* @file hkdf_hardware.hpp
 * @brief Zero-Allocation HKDF and HMAC using SHA-NI hardware.
 */

#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/endian.hpp"
#include "slabflux/security/sha256_hardware.hpp"

namespace slabflux::security {

    struct hmac_sha256_hardware {
        /**
         * @brief Zero-allocation HMAC-SHA256 evaluation.
         */
        static SLAB_HOT void compute(const uint8_t* key, size_t key_len, 
                                     const uint8_t* msg, size_t msg_len, 
                                     uint8_t out[32]) noexcept {
            alignas(64) uint8_t k[64] = {0};
            
            if (key_len > 64) {
                sha256_state st;
                st.update(key, key_len);
                st.finalize(k);
            } else {
                std::memcpy(k, key, key_len);
            }

            alignas(64) uint8_t ipad[64];
            alignas(64) uint8_t opad[64];
            for (int i = 0; i < 64; ++i) {
                ipad[i] = k[i] ^ 0x36;
                opad[i] = k[i] ^ 0x5C;
            }

            sha256_state inner;
            inner.update(ipad, 64);
            inner.update(msg, msg_len);
            
            uint8_t inner_hash[32];
            inner.finalize(inner_hash);

            sha256_state outer;
            outer.update(opad, 64);
            outer.update(inner_hash, 32);
            outer.finalize(out);
        }
    };

    struct hkdf_hardware {
        /**
         * @brief RFC 5869 HKDF-Extract.
         */
        static SLAB_HOT void extract(const uint8_t* salt, size_t salt_len,
                                     const uint8_t* ikm, size_t ikm_len,
                                     uint8_t prk[32]) noexcept {
            const uint8_t zero_salt[32] = {0};
            if (salt_len == 0) {
                salt = zero_salt;
                salt_len = 32;
            }
            hmac_sha256_hardware::compute(salt, salt_len, ikm, ikm_len, prk);
        }

        /**
         * @brief RFC 8446 TLS 1.3 HKDF-Expand-Label.
         * @details Derives traffic keys strictly within the L1 cache using stack-local synthesis.
         */
        static SLAB_HOT void expand_label(const uint8_t* secret, 
                                          const char* label, size_t label_len,
                                          const uint8_t* context, size_t context_len,
                                          size_t length, uint8_t* out) noexcept {
            
            // Dynamically construct HkdfLabel structure without heap allocation
            alignas(64) uint8_t hkdf_label[512];
            size_t pos = 0;

            uint16_t net_len = core::endian::host_to_network16(static_cast<uint16_t>(length));
            std::memcpy(hkdf_label + pos, &net_len, 2);
            pos += 2;

            const char* prefix = "tls13 ";
            size_t prefix_len = 6;
            uint8_t total_label_len = static_cast<uint8_t>(prefix_len + label_len);
            
            hkdf_label[pos++] = total_label_len;
            std::memcpy(hkdf_label + pos, prefix, prefix_len);
            pos += prefix_len;
            std::memcpy(hkdf_label + pos, label, label_len);
            pos += label_len;

            hkdf_label[pos++] = static_cast<uint8_t>(context_len);
            if (context_len > 0) {
                std::memcpy(hkdf_label + pos, context, context_len);
                pos += context_len;
            }

            // Standard HKDF-Expand Execution (T(1) = HMAC-Hash(PRK, T(0) | info | 0x01))
            alignas(64) uint8_t msg[32 + 512 + 1]; 
            uint8_t t[32];
            size_t generated = 0;
            uint8_t counter = 1;

            while (generated < length) {
                size_t msg_len = 0;
                if (counter > 1) {
                    std::memcpy(msg, t, 32);
                    msg_len += 32;
                }
                
                std::memcpy(msg + msg_len, hkdf_label, pos);
                msg_len += pos;
                
                msg[msg_len++] = counter++;

                hmac_sha256_hardware::compute(secret, 32, msg, msg_len, t);

                size_t chunk = (length - generated < 32) ? (length - generated) : 32;
                std::memcpy(out + generated, t, chunk);
                generated += chunk;
            }
        }
    };

} // namespace slabflux::security