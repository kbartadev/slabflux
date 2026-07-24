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
#include <sodium.h>
#include <concepts>
#include <cstdint>
#include "slabflux/core/wire_frame_lsn.hpp"
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::security {

    /**
     * @brief Compile-Time Cryptographic Trait Generator.
     * @details Statically derives the physical envelopes for authentication 
     * payloads by synthesizing the relationship between algorithm widths and 
     * hardware alignment.
     */
    struct ed25519_traits {
        static constexpr std::size_t signature_size = crypto_sign_BYTES;
        static constexpr std::size_t public_key_size = crypto_sign_PUBLICKEYBYTES;
        static constexpr std::size_t ID = 0xED25519;

        static SLAB_FORCE_INLINE bool verify(const unsigned char* sig, const unsigned char* msg, 
                                            std::size_t len, const unsigned char* pk) noexcept {
            return ::crypto_sign_verify_detached(sig, msg, len, pk) == 0;
        }
    };

    /**
     * @brief Synthesized Secure Frame.
     * @details Implements an algorithm-bound meta matrix, statically 
     * aligned to 64-byte boundaries to 
     * eliminate TLB misses during high-frequency decryption pulses.
     */
    template <typename AlgorithmTraits>
    struct alignas(64) secure_frame {
        unsigned char signature[AlgorithmTraits::signature_size];
        unsigned char public_key[AlgorithmTraits::public_key_size];
        uint64_t      client_sequence;
        
        // Synthesis: Physical padding derived to maintain cache line sovereignty
        static constexpr std::size_t RAW_SIZE = AlgorithmTraits::signature_size + 
                                                AlgorithmTraits::public_key_size + sizeof(uint64_t);
        char _padding[(64 - (RAW_SIZE % 64)) % 64];
    };

    template<typename Payload, typename Traits = ed25519_traits>
    class secure_ingress_validator {
    public:
        /**
         * @brief Constant-time evaluation for security tokens.
         * @details Replaces generic loops with a hardware-serialized comparison pulse.
         */
        static SLAB_FORCE_INLINE bool compare_constant_time(const void* a, const void* b, size_t len) noexcept {
            return ::sodium_memcmp(a, b, len) == 0;
        }

        /**
         * @brief Synthesized Authentication Pulse.
         * @details Dispatches verification logic based on the injected 
         * cryptographic traits, ensuring zero branch-overhead transitions.
         */
        static SLAB_FORCE_INLINE bool is_authentic(const core::wire_frame_lsn<Payload>& frame, 
                                                   const secure_frame<Traits>& sec) noexcept {
            return Traits::verify(sec.signature, 
                                 reinterpret_cast<const unsigned char*>(&frame.payload), 
                                 sizeof(Payload), 
                                 sec.public_key);
        }
    };

} // namespace slabflux::security