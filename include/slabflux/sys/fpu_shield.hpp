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
 */
#pragma once
#include <cstdint> // For uint32_t
#include <xmmintrin.h> // For _mm_getcsr, _mm_setcsr
#include "slabflux/core/hot_path_alignment.hpp" // For SLAB_FORCE_INLINE

namespace slabflux::sys {

    /**
     * @brief Constexpr FPU Control Word.
     * @details Defines the desired MXCSR state at compile time, acting as a
     * supervisor for the FPU context.
     */
    struct fpu_control_word {
        // MXCSR flags: FTZ (Flush to Zero, bit 15), DAZ (Denormals are Zero, bit 6)
        // Default MXCSR is 0x1F80.
        // Setting FTZ (0x8000) and DAZ (0x0040) results in 0x1F80 | 0x8000 | 0x0040 = 0x9FC0.
        static constexpr uint32_t MXCSR_FTZ_DAZ_ON = 0x9FC0;

        // Default MXCSR value for restoration.
        static constexpr uint32_t MXCSR_DEFAULT = 0x1F80;
    };

/**
 * @brief Hardware-level FPU isolation.
 * @details Enforces Flush-to-Zero (FTZ) and Denormals-are-Zero (DAZ)
 * to ensure O(1) mathematical throughput.
 */
class fpu_shield {
private:
    uint32_t original_mxcsr_; // To store the FPU state before engaging

public:
    // RAII constructor: engages the FPU shield
    fpu_shield() noexcept {
        // Capture current MXCSR before modifying
        original_mxcsr_ = _mm_getcsr(); // Use intrinsic to get current state
        engage();
    }

    // RAII destructor: disengages the FPU shield
    ~fpu_shield() noexcept {
        // Restore original MXCSR state
        asm volatile("ldmxcsr %0" : : "m"(original_mxcsr_) : "memory");
    }
    
private:
    // Static method to engage FPU shield (non-RAII usage)
    static SLAB_FORCE_INLINE void engage() noexcept {
        uint32_t mxcsr_val = fpu_control_word::MXCSR_FTZ_DAZ_ON;
        asm volatile("ldmxcsr %0" : : "m"(mxcsr_val) : "memory");
    }

    // Static method to disengage FPU shield (non-RAII usage)
    static SLAB_FORCE_INLINE void disengage() noexcept {
        uint32_t mxcsr_val = fpu_control_word::MXCSR_DEFAULT;
        asm volatile("ldmxcsr %0" : : "m"(mxcsr_val) : "memory");
    }
};

} // namespace slabflux::sys
