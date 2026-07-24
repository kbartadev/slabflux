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
#include "slabflux/core/physics.hpp"
#include "slabflux/core/conduit.hpp"
#include <immintrin.h> // For _mm_pause
#include "slabflux/core/event_guard.hpp"
#include "slabflux/core/pool.hpp"
#include <atomic>
#include <array>
#include <new>

namespace slabflux::core {

    // ========================================================================
    // SMART POINTER ALIASES
    // ========================================================================
    
    /** @brief Internal placeholder for unmanaged event lifetimes. */
    struct null_pool_base { static void static_deleter(void*, void*) noexcept {} };

    /** @brief The standard smart pointer for event lifecycle management. */
    template <typename T>
    using event_ptr = event_guard<T, null_pool_base>;

    // ========================================================================
    // THE 8-BYTE NETWORK PACKET
    // ========================================================================
    struct tagged_pointer {
        uint64_t data{0};

        static constexpr uint64_t TAG_MASK = 0xFFFF000000000000;
        static constexpr uint64_t PTR_MASK = 0x0000FFFFFFFFFFFF;

        SLAB_FORCE_INLINE static tagged_pointer pack(uint16_t tag, void* ptr) noexcept {
            return {(static_cast<uint64_t>(tag) << 48) | (reinterpret_cast<uint64_t>(ptr) & PTR_MASK)};
        }

        SLAB_FORCE_INLINE uint16_t tag() const noexcept { return static_cast<uint16_t>(data >> 48); }
        
        SLAB_FORCE_INLINE void* ptr() const noexcept { 
            // Hardware Canonical Address Enforcer.
            // A simple bitwise AND destroys the sign-extension of negative kernel/mmap addresses.
            // We force sign-extension via arithmetic shifts.
            return reinterpret_cast<void*>(static_cast<int64_t>(data << 16) >> 16);
        }
        SLAB_FORCE_INLINE void* get_address() const noexcept { return ptr(); }
    };

}
