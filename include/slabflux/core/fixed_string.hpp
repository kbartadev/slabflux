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
#include <cstring>
#include <string_view>
#include <algorithm>
#include <ostream>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::core {

/**
 * @brief Non-Type Template Parameter String Engine.
 * @details Implements a strictly bounded character matrix with compile-time 
 * projection logic. This structure eliminates runtime heuristics associated 
 * with small string optimization (SSO) by enforcing physical layout 
 * invariants at the type level.
 */
template <uint32_t Capacity>
struct fixed_string {
    static_assert(Capacity > 0, "Capacity must be greater than 0");

    char data_[Capacity + 1];
    uint32_t length_{0};

    constexpr fixed_string() noexcept : data_{0}, length_{0} {}

    /** @brief NTTP Engine: Constant-time initialization from string literals. */
    template <std::size_t N>
    constexpr fixed_string(const char (&str)[N]) noexcept : data_{0}, length_{0} {
        const std::size_t source_len = N - 1;
        length_ = static_cast<uint32_t>(source_len > Capacity ? Capacity : source_len);
        std::copy_n(str, length_, data_);
        data_[length_] = '\0';
    }

    explicit constexpr fixed_string(std::string_view sv) noexcept : data_{0}, length_{0} {
        assign(sv);
    }

    fixed_string& operator=(std::string_view sv) noexcept {
        assign(sv);
        return *this;
    }

    SLAB_FORCE_INLINE constexpr void assign(std::string_view sv) noexcept {
        length_ = static_cast<uint32_t>(std::min(static_cast<size_t>(Capacity), sv.length()));
        std::copy_n(sv.data(), length_, data_);
        data_[length_] = '\0'; // Guarantee null-termination for safe c_str() usage
    }

    constexpr void clear() noexcept {
        length_ = 0;
        data_[0] = '\0';
    }

    [[nodiscard]] constexpr std::string_view view() const noexcept { return {data_, length_}; }
    [[nodiscard]] constexpr const char* c_str() const noexcept { return data_; }
    [[nodiscard]] constexpr uint32_t size() const noexcept { return length_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return length_ == 0; }

    constexpr bool operator==(std::string_view sv) const noexcept { return view() == sv; }

    friend std::ostream& operator<<(std::ostream& os, const fixed_string& fs) {
        return os << fs.view();
    }
};

} // namespace slabflux::core