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
 * @brief High-Performance Platform Abstraction
 */

#pragma once
#include <fstream>
#include <atomic>

#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <memoryapi.h>
#include <winsock2.h>
#include <process.h> // Replacement for getpid()
#pragma comment(lib, "ws2_32.lib")

/** @brief Windows mmap shim protection flags */
#define PROT_READ      0x1
#define PROT_WRITE     0x2

/** @brief Windows mmap shim mapping flags (unrelated to Linux kernel hex codes) */
#define MAP_PRIVATE    0x01
#define MAP_ANONYMOUS  0x02
#define MAP_POPULATE   0x04
#define MAP_LOCKED     0x08
#define MAP_HUGETLB    0x10
#define MAP_HUGE_2MB   0x20
#define MAP_FAILED     ((void*)-1)

#if defined(_MSC_VER)
#include <immintrin.h>
#include <intrin.h>
#define __builtin_ctz(x) (int)_tzcnt_u32(x)
#define __builtin_ctzll(x) (int)_tzcnt_u64(x)
#endif

inline void* mmap(void* addr, size_t len, int prot, int flags, int fd, size_t offset) { // Qualified
    const DWORD win_prot = (prot & PROT_WRITE) ? PAGE_READWRITE : PAGE_READONLY;
    return VirtualAlloc(NULL, len, MEM_COMMIT | MEM_RESERVE, win_prot);
}

inline int munmap(void* addr, size_t len) { // Qualified
    return VirtualFree(addr, 0, MEM_RELEASE) ? 0 : -1;
}

inline int mlock(void* addr, size_t len) {
    return VirtualLock(addr, len) ? 0 : -1;
}

inline int munlock(const void* addr, size_t len) {
    return VirtualUnlock(const_cast<void*>(addr), len) ? 0 : -1;
}

// unistd.h equivalents
inline int getpid() { return (int)_getpid(); }
#else
#include <sys/mman.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/syscall.h>
#ifdef __linux__
#include <linux/futex.h>
#endif
#include <sched.h>
#include <fcntl.h>
#endif

namespace slabflux::os {

#if defined(_WIN32) || defined(_WIN64)
    using socket_t = SOCKET;
    using socklen_t = int;
    #define SLAB_INVALID_SOCKET INVALID_SOCKET
    
    inline int close_socket(socket_t s) { return ::closesocket(s); }
    inline int set_nonblocking(socket_t s) { 
        u_long mode = 1; 
        return ::ioctlsocket(s, FIONBIO, &mode); 
    }
#else
    using socket_t = int;
    using socklen_t = ::socklen_t;
    #define SLAB_INVALID_SOCKET (-1)

    inline int close_socket(socket_t s) { return ::close(s); }
    inline int set_nonblocking(socket_t s) { 
        return ::fcntl(s, F_SETFL, ::fcntl(s, F_GETFL, 0) | O_NONBLOCK); 
    }

    /**
     * @brief Futex Wait Wrapper using C++20 atomic primitives.
     */
    inline void futex_wait(std::atomic<uint64_t>* addr, uint64_t val) noexcept {
        addr->wait(val);
    }

    /**
     * @brief Futex Wake Wrapper using C++20 atomic primitives.
     */
    inline void futex_wake(std::atomic<uint64_t>* addr) noexcept {
        addr->notify_all();
    }

    /**
     * @brief Establishes a private network namespace.
     * @return 0 on success, or OS error code.
     */
    inline int establish_network_isolation() noexcept {
#ifdef __linux__
        return ::unshare(CLONE_NEWNET);
#else
        return 0;
#endif
    }
#endif

    // Socket Layer: Import standard identifiers for benchmark parity
    using ::socket; using ::bind; using ::listen; using ::getsockname;
    using ::connect; using ::accept;

    /** @brief Real-function wrapper to bypass macro-to-namespace resolution issues. */
    #undef htonl
    inline uint32_t htonl(uint32_t hostlong) noexcept {
        return ::htonl(hostlong);
    }

    using ::sockaddr; using ::sockaddr_in;

    /**
     * @brief Checks if the host OS is configured for HugePage allocation.
     * Used by tests to skip physical audits on non-HFT tuned environments (Linux).
     */
    inline bool has_hugepage_support() noexcept {
#if defined(_WIN32) || defined(_WIN64)
        return GetLargePageMinimum() > 0;
#else
        std::ifstream f("/proc/sys/vm/nr_hugepages");
        int count = 0;
        return (f >> count) && (count > 0);
#endif
    }
}
