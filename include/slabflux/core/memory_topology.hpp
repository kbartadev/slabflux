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
#include <cstdint>
#include <concepts>
#include <system_error> // For std::system_error
#include <iostream>     // For std::cerr
#include <stdexcept>    // For std::bad_alloc

#ifdef __linux__
#include <sys/mman.h>   // For mmap, mlock, munmap, munlock, madvise
#include <errno.h>      // For errno
#endif

namespace slabflux::core {

    /**
     * @brief Concept for a Node-Bound Memory Topology Policy.
     * @details Defines the contract for how memory is allocated, locked, and managed
     * to ensure physical residency and NUMA locality for SlabFlux components.
     */
    template <typename T>
    concept MemoryTopologyPolicy = requires(size_t size_bytes, void* ptr) {
        { T::allocate(size_bytes) } -> std::same_as<void*>;
        { T::deallocate(ptr, size_bytes) } -> std::same_as<void>;
        { T::lock(ptr, size_bytes) } -> std::same_as<void>;
        { T::unlock(ptr, size_bytes) } -> std::same_as<void>;
        { T::advise(ptr, size_bytes) } -> std::same_as<void>;
    };

    /**
     * @brief Default Memory Topology Policy for Linux.
     * @details Implements standard mmap/mlock/madvise with HugePage preference
     * and graceful fallback. This is the baseline for physical memory management.
     */
    struct DefaultLinuxMemoryTopology {
        static void* allocate(size_t size_bytes) {
#ifdef __linux__
            int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_LOCKED | MAP_HUGETLB | MAP_HUGE_2MB;
            void* mem = ::mmap(nullptr, size_bytes, PROT_READ | PROT_WRITE, flags, -1, 0);

            if (mem == MAP_FAILED && (flags & MAP_HUGETLB)) {
                flags &= ~(MAP_HUGETLB | MAP_HUGE_2MB); // Try without HugePages
                mem = ::mmap(nullptr, size_bytes, PROT_READ | PROT_WRITE, flags, -1, 0);
            }

            if (mem == MAP_FAILED) {
                flags = MAP_PRIVATE | MAP_ANONYMOUS; // Try without MAP_LOCKED
                mem = ::mmap(nullptr, size_bytes, PROT_READ | PROT_WRITE, flags, -1, 0);
                if (mem == MAP_FAILED) {
                    throw std::system_error(errno, std::generic_category(), "MemoryTopology: mmap failed");
                }
            }
            return mem;
#else
            // Fallback for non-Linux (e.g., Windows)
            void* mem = ::malloc(size_bytes);
            if (!mem) throw std::bad_alloc();
            return mem;
#endif
        }

        static void deallocate(void* ptr, size_t size_bytes) {
#ifdef __linux__
            if (ptr && ptr != MAP_FAILED) {
                ::munmap(ptr, size_bytes);
            }
#else
            ::free(ptr);
#endif
        }

        static void lock(void* ptr, size_t size_bytes) {
#ifdef __linux__
            // mlock hiba csendben lenyelve
            ::mlock(ptr, size_bytes);
#else
            // No-op or platform specific lock for non-Linux
#endif
        }

        static void unlock(void* ptr, size_t size_bytes) {
#ifdef __linux__
            if (ptr && ptr != MAP_FAILED) {
                ::munlock(ptr, size_bytes);
            }
#else
            // No-op or platform specific unlock for non-Linux
#endif
        }

        static void advise(void* ptr, size_t size_bytes) {
#ifdef __linux__
            ::madvise(ptr, size_bytes, MADV_HUGEPAGE | MADV_DONTDUMP);
#else
            // No-op or platform specific advise for non-Linux
#endif
        }
    };

    // Alias for the default policy based on OS
#ifdef __linux__
    using DefaultMemoryTopology = DefaultLinuxMemoryTopology;
#else
    using DefaultMemoryTopology = DefaultLinuxMemoryTopology; // Using Linux policy as a placeholder for now, or define a Windows specific one
#endif

} // namespace slabflux::core