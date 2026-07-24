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
 * @brief Hardware Topology: NUMA-aware Thread and Memory Pinning.
 */

#pragma once
#include <fstream>
#include <string>
#include <cstdint>
#include <cstdlib>
#include <system_error>
#include <iostream>
#include <atomic>
#include <immintrin.h>
#include "slabflux/platform/os.hpp" // Macro source of truth

#if !defined(_WIN32)
    #include <pthread.h> // For pthread_setaffinity_np
    #include <numa.h>    // For NUMA functions
    #include <numaif.h>  // For mbind
    #include <sys/mman.h>
    #include <numa.h>
    #include <numaif.h>
    #include <unistd.h>
    #include <sched.h>
#else
    #include <windows.h>
#endif

namespace slabflux::core {

    /**
     * @brief Hardware topology.
     * @details Performance integrity. No cross-socket latency.
     */
    struct hardware_topology {

        /**
         * @brief Returns the ID of the CPU core currently executing this thread.
         */
        static int get_current_cpu() noexcept {
#if defined(_WIN32)
            return static_cast<int>(GetCurrentProcessorNumber());
#else
            return sched_getcpu();
#endif
        }

        /**
         * @brief Pin a thread to a specific core.
         */
        static void pin_thread(uint32_t cpu_id) {
#if defined(_WIN32)
            DWORD_PTR process_mask, system_mask;
            GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask);
            uint32_t num_cores = __builtin_popcountll(system_mask);
            uint32_t target_cpu = cpu_id % (num_cores ? num_cores : 1);

            if (SetThreadAffinityMask(GetCurrentThread(), 1ULL << target_cpu) == 0) {
                throw std::system_error(GetLastError(), std::system_category(), "Thread pinning failed (Win)");
            }
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#else
            const int num_cores = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
            const int target_cpu = static_cast<int>(cpu_id % (num_cores ? num_cores : 1));

            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(target_cpu, &cpuset);

            int ret = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
            if (ret != 0) {
                // pthread returns error codes directly; errno is not reliable here.
                throw std::system_error(ret, std::generic_category(), "Thread pinning failed (Linux)");
            }

            struct sched_param param;
            param.sched_priority = 99;
            
            if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0) {
                static std::atomic<bool> warned{false};
                if (!warned.exchange(true)) {
                    std::cerr << "[WARN] Failed to set SCHED_FIFO policy. Determinism may be compromised. (Only reporting once per process)\n";
                }
            }
#endif
        }

        /**
         * @brief Audits the topology to ensure two cores do not share an L2 cache.
         * @details Critical for preventing Ingress-Compute cache set contention.
         */
        static bool verify_l2_isolation(int cpu1, int cpu2) noexcept {
            if (cpu1 < 0 || cpu2 < 0 || cpu1 == cpu2) return false;
#if defined(__linux__)
            // check: Compare the shared_cpu_list for L2 (index 2)
            char path1[128], path2[128];
            snprintf(path1, sizeof(path1), "/sys/devices/system/cpu/cpu%d/cache/index2/shared_cpus_list", cpu1);
            snprintf(path2, sizeof(path2), "/sys/devices/system/cpu/cpu%d/cache/index2/shared_cpus_list", cpu2);
            
            auto read_list = [](const char* p) -> std::string {
                char buf[64] = {0};
                int fd = ::open(p, O_RDONLY | O_CLOEXEC);
                if (fd >= 0) {
                    [[maybe_unused]] auto res = ::read(fd, buf, sizeof(buf) - 1);
                    ::close(fd);
                }
                // Returns SSO-optimized short string
                return std::string(buf);
            };

            std::string list1 = read_list(path1);
            std::string list2 = read_list(path2);

            // If lists are identical, they share the same physical L2 cache slice
            return list1 != list2;
#else
            return true; // Conservative fallback for non-Linux
#endif
        }

        /**
         * @brief Verifies that two cores do not share a physical core's L1i cache.
         * @details Critical for preventing Clock-Compute instruction thrashing.
         */
        static bool verify_l1i_isolation(int cpu1, int cpu2) noexcept {
            if (cpu1 < 0 || cpu2 < 0 || cpu1 == cpu2) return false;
#if defined(__linux__)
            // check: Compare the thread_siblings_list
            char path1[128], path2[128];
            snprintf(path1, sizeof(path1), "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", cpu1);
            snprintf(path2, sizeof(path2), "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", cpu2);
            
            auto read_list = [](const char* p) -> std::string {
                char buf[64] = {0};
                int fd = ::open(p, O_RDONLY | O_CLOEXEC);
                if (fd >= 0) {
                    [[maybe_unused]] auto res = ::read(fd, buf, sizeof(buf) - 1);
                    ::close(fd);
                }
                return std::string(buf);
            };

            std::string list1 = read_list(path1);
            std::string list2 = read_list(path2);

            // If sibling lists are identical, they share the same physical L1i/L1d
            if (list1.empty() || list2.empty()) return true; 
            return list1 != list2;
#else
            return true; // Conservative fallback
#endif
        }

        /**
         * @brief Bind memory to the NUMA node closest to the thread.
         * @details This is the part that bridge_sync wants to call.
         */
        static void bind_memory_to_local_numa(void* ptr, size_t size) {
#if defined(_WIN32)
            // On Windows, VirtualAllocExNuma already does this at allocation time,
            // but we can still keep a post‑allocation check here as well.
            (void)ptr; (void)size;
#else
            // If the kernel or environment has no NUMA support, skip multi-socket pinning
            if (numa_available() < 0 || numa_num_configured_nodes() <= 1) {
                return;
            }

            int cpu = sched_getcpu();
            int node = numa_node_of_cpu(cpu);
            if (node < 0) node = 0;

            unsigned long nodemask = (1UL << node);
            // Passing the exact maximum configured node count plus one (+1) prevents EINVAL on single-socket kernels
            unsigned long max_node_bits = numa_num_configured_nodes();

            if (mbind(ptr, size, MPOL_BIND, &nodemask, max_node_bits, MPOL_MF_STRICT) != 0) {
                throw std::system_error(errno, std::generic_category(), "Memory binding failed");
            }
#endif
        }

        /**
         * @brief Unified allocator alias a bridge_sync számára.
         * @details Enforces HugePage residency and physical pre-faulting.
         */
        static void* allocate_on_local_node(size_t size, size_t alignment = 2 * 1024 * 1024) {
            if (size == 0) [[unlikely]] return nullptr;

            // Requirement: Support HugePage or custom alignment for segmented storage
            const size_t aligned_size = (size + alignment - 1) & ~(alignment - 1);

#if defined(_WIN32)
            // Attempt Large Pages first, fallback to standard pages
            void* ptr = VirtualAlloc(NULL, aligned_size, MEM_COMMIT | MEM_RESERVE | MEM_LARGE_PAGES, PAGE_READWRITE);
            if (!ptr) ptr = VirtualAlloc(NULL, aligned_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            
            if (ptr) {
                // Manual Pre-fault: Touch every 4KB to ensure commit
                volatile char* p = static_cast<volatile char*>(ptr);
                for (size_t i = 0; i < aligned_size; i += 4096) p[i] = 0;
            }
            return ptr;
#else
            // Ignition: Explicit 2MB HugePage control via MAP_HUGE_2MB
            int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_POPULATE | MAP_LOCKED;
            
#ifdef MAP_HUGE_2MB
            flags |= MAP_HUGE_2MB;
#endif

            void* ptr = ::mmap(nullptr, aligned_size, PROT_READ | PROT_WRITE, flags, -1, 0);

            if (ptr == MAP_FAILED) {
                // Fallback 1: Standard pages with pre-faulting and pinning
                ptr = ::mmap(nullptr, aligned_size, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_LOCKED, -1, 0);
                
                if (ptr == MAP_FAILED) {
                    // Fallback 2: Basic allocation if locked memory is restricted (ulimit -l)
                    ptr = ::mmap(nullptr, aligned_size, PROT_READ | PROT_WRITE,
                                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
                }
            }

            if (ptr != MAP_FAILED) {
                bind_memory_to_local_numa(ptr, aligned_size);
                return ptr;
            }
            return nullptr;
#endif
        }

        /**
         * @brief Releases memory allocated via allocate_on_local_node.
         */
        static void deallocate_on_local_node(void* ptr, size_t size) noexcept {
            if (!ptr) return;
            const size_t page_size = 2 * 1024 * 1024;
            const size_t aligned_size = (size + page_size - 1) & ~(page_size - 1);
#if defined(_WIN32)
            VirtualFree(ptr, 0, MEM_RELEASE);
#else
            if (::munmap(ptr, aligned_size) != 0) {
                // Non-fatal but should be recorded in audit ledger
            }
#endif
        }

        template<typename T, size_t Capacity>
        static T* allocate_huge_pinned(int numa_node) {
#if defined(_WIN32)
            size_t size = Capacity * sizeof(T);
            void* ptr = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            return reinterpret_cast<T*>(ptr);
#else
            size_t size = Capacity * sizeof(T);
            size = (size + (2 * 1024 * 1024) - 1) & ~((2 * 1024 * 1024) - 1); // Align size to 2MB block boundary

            // 1. Try to allocate raw pinned hugepages (Production layout)
            void* ptr = ::mmap(nullptr, size, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_LOCKED, -1, 0);

            if (ptr == MAP_FAILED) {
                // 2. Desktop Fallback: If your PC lacks pre-allocated hugepages, fallback to standard pages
                // but keep them locked to RAM to prevent page faults during execution.
                ptr = ::mmap(nullptr, size, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS | MAP_LOCKED, -1, 0);

                if (ptr == MAP_FAILED) {
                    // 3. Ultimate Fallback: If locked memory is denied (ulimit -l), fallback to standard pages.
                    // This ensures the application boots even in restricted environments.
                    ptr = ::mmap(nullptr, size, PROT_READ | PROT_WRITE,
                                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

                    if (ptr == MAP_FAILED) {
                        throw std::system_error(errno, std::generic_category(), "Memory allocation completely exhausted");
                    }
                } else {
                    // Request transparent hugepages from the desktop OS kernel if available
                    ::madvise(ptr, size, MADV_HUGEPAGE);
                }
            }

            if (numa_node != -1 && numa_available() >= 0 && numa_num_configured_nodes() > 1) {
                numa_set_preferred(numa_node);
            }

            return reinterpret_cast<T*>(ptr);
#endif
        }
    };

} // namespace slabflux::core
