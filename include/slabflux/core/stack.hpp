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
 * @file stack.hpp
 * @brief Manual Stack Management.
 * @details Pivots the CPU stack pointer to a pre-allocated Hugepage 
 * region to eliminate kernel-managed stack jitter.
 */

#pragma once

#include <cstdint>
#include <sys/mman.h>
#include <pthread.h>
#include <stdexcept>
#include <unistd.h>

namespace slabflux::core {

    class thread_stack_allocator {
    public:
        /**
         * @brief Allocates a HugePage-backed stack and applies it to a thread attribute.
         * @details Micro-Architectural Cache Coloring & Guard Pages.
         * Replaces standard POSIX thread attributes with a physically colored 
         * memory mapping to eliminate L1 cache set contention across identical CPU cores.
         */
        static void* allocate_hugepage_stack(pthread_attr_t& attr, int core_id, size_t stack_size = 2 * 1024 * 1024) {
            // 1. Allocate 2MB HugePage for the thread stack
            void* stack_ptr = ::mmap(nullptr, stack_size, PROT_READ | PROT_WRITE,
                                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_STACK, -1, 0);
                                     
            if (stack_ptr == MAP_FAILED) {
                // Fallback to standard locked pages
                stack_ptr = ::mmap(nullptr, stack_size, PROT_READ | PROT_WRITE,
                                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
                if (stack_ptr == MAP_FAILED) throw std::bad_alloc();
            }
            
            ::mlock(stack_ptr, stack_size); // Pin stack to RAM

            // 2. Hardware Bounds-Check Guard Page
            // Instantly traps overflows via MMU Page Fault without software bounds checking.
            ::mprotect(stack_ptr, sysconf(_SC_PAGESIZE), PROT_NONE);

            // 3. Cache-Set Coloring: Offset the stack pointer to prevent physical aliasing
            size_t cache_color_offset = (core_id % 8) * 64;
            char* colored_stack_ptr = static_cast<char*>(stack_ptr) + sysconf(_SC_PAGESIZE) + cache_color_offset;
            size_t colored_size = stack_size - sysconf(_SC_PAGESIZE) - cache_color_offset;

            // 4. Safely instruct the OS to use this memory for the new thread
            if (::pthread_attr_setstack(&attr, colored_stack_ptr, colored_size) != 0) {
                ::munmap(stack_ptr, stack_size);
                throw std::runtime_error("Failed to set pthread custom stack");
            }
            
            return stack_ptr;
        }
    };
}