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
#include <cstddef>

#include <immintrin.h> // For _mm_pause
namespace slabflux::transport {

    /**
     * @brief Pure Protocol-Agnostic L4/L5 Transport Session State Window.
     * @details Completely detached from Layer-7 concepts to ensure infinite scaling and protocol reuse.
     * @tparam ProtocolState The private compile-time parsing state structure required by the L7 parser.
     * @tparam BufferCapacity The size of the raw streaming ring buffer arena.
     */
    template <typename ProtocolState, std::size_t BufferCapacity = 524288>
    struct alignas(64) session_context {
        // Pure transport metrics - Agnostic to HTTP, FIX, or WebSockets
        char buffer[BufferCapacity];
        std::size_t accumulated_bytes{0};
        std::size_t read_offset{0};

        // Enforce structural continuity by injecting the L7 parsing state as a black box
        alignas(64) ProtocolState protocol_state;

        /**
         * @brief Clears the active transport buffers and resets the protocol state machine.
         */
        void clear() noexcept {
            accumulated_bytes = 0;
            read_offset = 0;
            
            // Check if the protocol state provides an explicit reset capability at compile time
            if constexpr (requires(ProtocolState s) { s.reset(); }) {
                protocol_state.reset();
            } else {
                protocol_state = ProtocolState{};
            }
        }
    };

} // namespace slabflux::transport