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

#include "string_chunk.hpp"
#include "string_service.hpp"
#include "global_string_pool.hpp"
#include <string_view>
#include <string>
#include <ostream>

namespace slabflux::core {

// ============================================================
// THE BASE TEMPLATE
// ============================================================
template<typename ChunkPool>
class basic_smart_string {
    using service_type = string_service<ChunkPool>;

    service_type* svc_;
    fragmented_string fs_{};

public:
    // --- CONSTRUCTORS ---
    basic_smart_string() noexcept
        : svc_(&get_global_string_service()) {}

    basic_smart_string(std::string_view sv)
        : svc_(&get_global_string_service()) {
        svc_->assign(fs_, sv);
    }

    basic_smart_string(const char* str)
        : svc_(&get_global_string_service()) {
        svc_->assign(fs_, std::string_view(str));
    }

    basic_smart_string(service_type& svc, std::string_view sv)
        : svc_(&svc) {
        svc_->assign(fs_, sv);
    }

    ~basic_smart_string() {
        svc_->clear(fs_);
    }

    // --- MOVE & COPY ---
    basic_smart_string(basic_smart_string&& other) noexcept
        : svc_(other.svc_), fs_(other.fs_) {
        other.fs_.head_idx = string_chunk::END_OF_CHAIN;
        other.fs_.total_length = 0;
        other.fs_.sso_size = 0;
    }

    basic_smart_string& operator=(basic_smart_string&& other) noexcept {
        if (this != &other) {
            svc_->clear(fs_);
            svc_ = other.svc_;
            fs_ = other.fs_;
            other.fs_.head_idx = string_chunk::END_OF_CHAIN;
            other.fs_.total_length = 0;
            other.fs_.sso_size = 0;
        }
        return *this;
    }

    basic_smart_string(const basic_smart_string& other)
        : svc_(other.svc_) {
        svc_->assign(fs_, other.fs_);
    }

    basic_smart_string& operator=(const basic_smart_string& other) {
        if (this != &other) {
            svc_->assign(fs_, other.fs_);
        }
        return *this;
    }

    // --- BASIC API ---
    [[nodiscard]] size_t size() const noexcept { return fs_.total_length; }
    [[nodiscard]] size_t length() const noexcept { return fs_.total_length; }
    [[nodiscard]] bool empty() const noexcept { return fs_.total_length == 0; }
    void clear() { svc_->clear(fs_); }

    // --- ASSIGNMENT (=) EXPLICIT OVERLOADS ---
    basic_smart_string& operator=(std::string_view sv) { svc_->assign(fs_, sv); return *this; }
    basic_smart_string& operator=(const std::string& str) { svc_->assign(fs_, str); return *this; }
    basic_smart_string& operator=(const char* str) { svc_->assign(fs_, std::string_view(str)); return *this; }

    // --- APPEND (+=) ---
    basic_smart_string& operator+=(std::string_view sv) { svc_->append(fs_, sv); return *this; }
    basic_smart_string& operator+=(const std::string& str) { svc_->append(fs_, str); return *this; }
    basic_smart_string& operator+=(const char* str) { svc_->append(fs_, std::string_view(str)); return *this; }
    basic_smart_string& operator+=(const basic_smart_string& other) { svc_->append(fs_, other.fs_); return *this; }

    // --- COMPARISON (==, !=) ---
    [[nodiscard]] bool operator==(std::string_view sv) const noexcept { return svc_->equals(fs_, sv); }
    [[nodiscard]] bool operator==(const std::string& str) const noexcept { return svc_->equals(fs_, str); }
    [[nodiscard]] bool operator==(const char* str) const noexcept { return svc_->equals(fs_, std::string_view(str)); }
    [[nodiscard]] bool operator==(const basic_smart_string& other) const noexcept { return svc_->equals(fs_, other.fs_); }

    [[nodiscard]] bool operator!=(std::string_view sv) const noexcept { return !svc_->equals(fs_, sv); }
    [[nodiscard]] bool operator!=(const basic_smart_string& other) const noexcept { return !svc_->equals(fs_, other.fs_); }

    // --- CONVERSIONS & STREAM ---
    [[nodiscard]] std::string to_string() const { return svc_->extract_to_std_string(fs_); }
    operator std::string() const { return to_string(); }

    // Stream operator (guaranteed to exist)
    friend std::ostream& operator<<(std::ostream& os, const basic_smart_string& ss) {
        return os << ss.to_string();
    }
};

// ============================================================
// DEVELOPER-FRIENDLY ALIAS
// ============================================================
using smart_string = basic_smart_string<global_string_pool>;

} // namespace slabflux::core
