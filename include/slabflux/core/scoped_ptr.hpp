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
#include <utility>
#include <cstdint>
#include <assert.h>

namespace slabflux::core {

    // This class is “clear”. It doesn’t care what T is,
    // only what the cleanup protocol is.
    template <typename T>
    class scoped_ptr {
        T* ptr_;
        void (*deleter_)(void*, void*);
        void* pool_ctx_;

    public:
        // Construction: a raw pointer + a cleanup function + context
        scoped_ptr(T* p, void (*del)(void*, void*), void* ctx) noexcept
            : ptr_(p), deleter_(del), pool_ctx_(ctx) {}

        // Destructor: the place for deterministic cleanup
        ~scoped_ptr() {
            if (ptr_ && deleter_) {
                deleter_(pool_ctx_, ptr_);
            }
        }

        // Move semantics: transferring cleanup ownership (important!)
        scoped_ptr(scoped_ptr&& other) noexcept
            : ptr_(other.ptr_), deleter_(other.deleter_), pool_ctx_(other.pool_ctx_) {
            other.ptr_ = nullptr;
        }

        scoped_ptr& operator=(scoped_ptr&& other) noexcept {
            if (this != &other) {
                if (ptr_ && deleter_) deleter_(pool_ctx_, ptr_);
                ptr_ = other.ptr_;
                deleter_ = other.deleter_;
                pool_ctx_ = other.pool_ctx_;
                other.ptr_ = nullptr;
            }
            return *this;
        }

        T* release() noexcept {
            assert(ptr_ != nullptr && "Attempted to release an empty scoped_ptr!");
            T* temp = ptr_;
            ptr_ = nullptr;
            return temp;
        }

        // Copying disabled: must not be cleaned up twice!
        scoped_ptr(const scoped_ptr&) = delete;
        scoped_ptr& operator=(const scoped_ptr&) = delete;

        explicit operator bool() const noexcept {
            return ptr_ != nullptr;
        }

        // Access to the data (like a normal pointer)
        T* operator->() const noexcept { return ptr_; }
        T& operator*() const noexcept { return *ptr_; }
        T* get() const noexcept { return ptr_; }
    };
}
