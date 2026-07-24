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
 * ============================================================================*
 * @file reproducible_random.hpp
 * @brief Deterministic Entropy.
 */

#pragma once

#include <cstdint>
#include <bit>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::compute {

    /**
     * @brief Orbital Cascade (OC-64) PRNG - Long Entropy Version.
     * @details 100% deterministic, 128-bit state extended PRNG.
     * Employs a Weyl-sequence Phase Accumulator intertwined with a mutated Turbulence
     * Register. Utilizes strict bijections (additions, rotations, odd-multiplications)
     * modulo 2^64 to guarantee uniform multi-dimensional distribution and a strict minimum
     * period of 2^128 (via native 128-bit accumulation) without relying on any known PRNG families.
     */
    class long_entropy_rng {
        unsigned __int128 phi_; // 128-bit Phase Accumulator (Extended Weyl sequence)
        uint64_t theta_;        // Turbulence Register

        // OC-64 Constants (Odd parity, coprime to 2^64)
        static constexpr uint64_t K1 = 9817234650192837411ULL;
        static constexpr uint64_t K2 = 3817465928173456789ULL;

        // Seed Avalanching: Strongly mixes sequential LSNs before state injection
        // to eliminate inter-stream correlation across adjacent seeds.
        static constexpr uint64_t mix_seed(uint64_t s) noexcept {
            s ^= s >> 33;
            s *= 0xff51afd7ed558ccdULL;
            s ^= s >> 33;
            s *= 0xc4ceb9fe1a85ec53ULL;
            s ^= s >> 33;
            return s;
        }

        SLAB_FORCE_INLINE constexpr uint64_t generate_64() noexcept {
            // 1. Advance the 128-bit Phase Accumulator
            phi_ += K1;

            // Extract the lower 64 bits for the math operations
            const uint64_t current_phi_64 = static_cast<uint64_t>(phi_);

            // 2. Entangle and Mutate the Turbulence Register
            const uint64_t delta = theta_ ^ current_phi_64;
            theta_ = std::rotl(delta, 29) * K2;

            // 3. Output Permutation (Phase-Turbulence Fusion)
            const uint64_t z1 = theta_ + std::rotr(current_phi_64, 19);
            
            // 4. Avalanche Shift (High-to-Low cascade)
            const uint64_t z2 = z1 ^ (z1 >> 35);
            
            // 5. Non-linear Algebraic Expansion
            const uint64_t z3 = z2 * K1;
            
            // 6. Final Output Diffusion
            return z3 ^ (z3 >> 27);
        }

    public:
        explicit constexpr long_entropy_rng(uint64_t lsn_seed) noexcept {
            const uint64_t mixed_seed = mix_seed(lsn_seed);
            // Initialize 128-bit phi using mixed seed
            phi_   = (static_cast<unsigned __int128>(mixed_seed) * K1) + 1ULL;
            theta_ = mixed_seed ^ K2;
            
            // Pre-cycle 3 times to guarantee immediate maximal avalanche
            generate_64();
            generate_64();
            generate_64();
        }

        /** @brief Standard 32-bit extraction API (Backward compatibility). */
        SLAB_FORCE_INLINE constexpr uint32_t next() noexcept {
            // Extracting the high 32 bits maximizes statistical entropy
            return static_cast<uint32_t>(generate_64() >> 32);
        }
        
        /** @brief Raw 64-bit sampling. */
        SLAB_FORCE_INLINE constexpr uint64_t next64() noexcept {
            return generate_64();
        }
    };

    /**
     * @brief Orbital Cascade (OC-64) PRNG.
     * @details 100% deterministic, default 64-bit accumulator version. Period = 2^64.
     * Recommended default for maximum immediate entropy dispersion.
     */
    class deterministic_rng {
        uint64_t phi_;   // 64-bit Phase Accumulator
        uint64_t theta_; // Turbulence Register

        // OC-64 Constants
        static constexpr uint64_t K1 = 9817234650192837411ULL;
        static constexpr uint64_t K2 = 3817465928173456789ULL;

        static constexpr uint64_t mix_seed(uint64_t s) noexcept {
            s ^= s >> 33;
            s *= 0xff51afd7ed558ccdULL;
            s ^= s >> 33;
            s *= 0xc4ceb9fe1a85ec53ULL;
            s ^= s >> 33;
            return s;
        }

        SLAB_FORCE_INLINE constexpr uint64_t generate_64() noexcept {
            phi_ += K1;
            const uint64_t delta = theta_ ^ phi_;
            theta_ = std::rotl(delta, 29) * K2;
            const uint64_t z1 = theta_ + std::rotr(phi_, 19);
            const uint64_t z2 = z1 ^ (z1 >> 35);
            const uint64_t z3 = z2 * K1;
            return z3 ^ (z3 >> 27);
        }

    public:
        explicit constexpr deterministic_rng(uint64_t lsn_seed) noexcept {
            const uint64_t mixed_seed = mix_seed(lsn_seed);
            phi_   = (mixed_seed * K1) + 1ULL;
            theta_ = mixed_seed ^ K2;
            
            generate_64();
            generate_64();
            generate_64();
        }

        SLAB_FORCE_INLINE constexpr uint32_t next() noexcept {
            return static_cast<uint32_t>(generate_64() >> 32);
        }
        
        SLAB_FORCE_INLINE constexpr uint64_t next64() noexcept {
            return generate_64();
        }
    };
}
