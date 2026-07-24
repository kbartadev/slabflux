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
#include <cstdint>
#include <cstddef>

namespace slabflux::sys {

/**
 * @brief Relative pointer for memory relocatability.
 * @details Stores the offset between 'this' and the target address.
 */
template<typename T>
class offset_ptr {
    std::ptrdiff_t offset_;

public:
    offset_ptr() : offset_(0) {}
    explicit offset_ptr(T* ptr) {
        if (!ptr) offset_ = 0;
        else offset_ = reinterpret_cast<char*>(ptr) - reinterpret_cast<char*>(this);
    }

    inline T* get() const noexcept {
        if (offset_ == 0) return nullptr;
        return reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(this) + offset_);
    }

    inline T* operator->() const noexcept { return get(); }
    inline T& operator*() const noexcept { return *get(); }

    [[nodiscard]] inline std::uintptr_t raw_offset() const noexcept {
        // In the test, this is used to verify the relative gap
        return static_cast<std::uintptr_t>(offset_ > 0 ? offset_ : -offset_);
    }

    static_assert(sizeof(std::ptrdiff_t) == 8, "offset_ptr requires 64-bit architecture");
};

} // namespace slabflux::sys
