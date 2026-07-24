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

namespace slabflux::core {
    // Zero-cost RAII wrapper for automatic cross-thread memory reclamation
    template <typename T, typename Pool>
    struct event_guard {
        T* ptr_{nullptr};
        Pool* pool_{nullptr};

        event_guard() = default;
        event_guard(T* p, Pool* pool) noexcept : ptr_(p), pool_(pool) {}

        ~event_guard() {
            if (ptr_ && pool_) pool_->static_deleter(pool_, ptr_);
        }

        // Move-only
        event_guard(event_guard&& other) noexcept : ptr_(other.ptr_), pool_(other.pool_) {
            other.ptr_ = nullptr;
        }
        event_guard& operator=(event_guard&& other) noexcept {
            if (this != &other) {
                if (ptr_ && pool_) pool_->static_deleter(pool_, ptr_);
                ptr_ = other.ptr_;
                pool_ = other.pool_;
                other.ptr_ = nullptr;
            }
            return *this;
        }

        event_guard(const event_guard&) = delete;
        event_guard& operator=(const event_guard&) = delete;

        T* release() noexcept { T* tmp = ptr_; ptr_ = nullptr; return tmp; }
        T* get() const noexcept { return ptr_; }
        T& operator*() const noexcept { return *ptr_; }
        T* operator->() const noexcept { return ptr_; }
        explicit operator bool() const noexcept { return ptr_ != nullptr; }
    };
}
