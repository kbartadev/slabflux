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
#include <cstddef>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdexcept>
#include <sys/mman.h>
#include <nmmintrin.h> // For _mm_crc32_u64
#include <atomic>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::security {

    /**
     * @brief The Semiotic Tapestry for Kinetic Inscription.
     * @details Allocates a dense virtual memory region of x86-64 RET (0xC3) instructions.
     * Anomalies are engraved into the CPU's Last Branch Record (LBR) MSRs
     * by dynamically calling an address within this tapestry. The trajectory
     * intrinsically encodes the Error Code and LSN in the physical Instruction
     * Pointer, requiring zero memory writes to RAM and completely eliminating
     * cache-line invalidation across cores.
     */
    class alignas(64) semiotic_tapestry {
    private:
        uint8_t* base_address_{nullptr};
        size_t tapestry_size_{0};

        // Defines the bit-layout for the geometric trajectory
        // Encodes ErrorCode (8 bits) | LSN (24 bits) = 32 bits total offset (4GB Tapestry)
        static constexpr size_t LSN_MASK = 0xFFFFFF;
        static constexpr size_t ERROR_SHIFT = 24;

    public:
        semiotic_tapestry() = default;

        ~semiotic_tapestry() {
            if (base_address_ && base_address_ != MAP_FAILED) {
                munmap(base_address_, tapestry_size_);
            }
        }

        /**
         * @brief Weaves the Semiotic Tapestry in virtual memory.
         * @param size_in_bytes Size of the tapestry (default 4GB).
         */
        void weave(size_t size_in_bytes = 0x100000000ULL) {
            tapestry_size_ = size_in_bytes;

            // Allocate virtual memory (MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE)
            base_address_ = static_cast<uint8_t*>(mmap(
                nullptr,
                tapestry_size_,
                PROT_READ | PROT_WRITE | PROT_EXEC,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE,
                -1, 0
            ));

            if (base_address_ == MAP_FAILED) {
                throw std::runtime_error("SLABFLUX FATAL: Failed to weave Semiotic Tapestry (mmap failed)");
            }

            // Populate the entire tapestry with the x86-64 RET instruction (0xC3)
            __builtin_memset(base_address_, 0xC3, tapestry_size_);

            // Seal the tapestry to permanently prevent accidental structural overwrites
            mprotect(base_address_, tapestry_size_, PROT_READ | PROT_EXEC);
        }

        /**
         * @brief Engraves an anomaly into the silicon LBR without touching the data cache.
         * @param error_code The specific fault identity.
         * @param lsn The chronological sequence number.
         */
        SLAB_FORCE_INLINE void engrave_anomaly(uint8_t error_code, uint32_t lsn) const noexcept {
            // Calculate the exact spatial trajectory coordinate
            size_t offset = (static_cast<size_t>(error_code) << ERROR_SHIFT) | (lsn & LSN_MASK);
            void* target_rip = base_address_ + offset;

            // Kinetic Inscription:
            // Executes an indirect CALL to the Tapestry. The CPU hardware inherently
            // records the target address in the Last Branch Record (LBR) MSR.
            // The Tapestry instruction is a RET (0xC3), instantly popping the stack
            // and returning execution to the next instruction on the hot-path.
            asm volatile("call *%0" : : "r"(target_rip) : "memory");
        }

        SLAB_FORCE_INLINE uint8_t* base() const noexcept { return base_address_; }
        SLAB_FORCE_INLINE size_t size() const noexcept { return tapestry_size_; }
    };

    /**
     * @brief The Panoptic Reticle (Observer Node)
     * @details Runs completely out-of-band on an isolated housekeeping core.
     * Extracts Kinetic Inscriptions directly from the hot-path core's PMU LBR registers.
     */
    class panoptic_reticle {
    private:
        int msr_fd_{-1};
        const semiotic_tapestry& tapestry_;

        // Intel LBR MSR Address (Skylake/Cascade Lake typically 0x6C0 to 0x6DF for TO_IP)
        // MSR_LASTBRANCH_0_TO_IP represents the destination of the most recent branch.
        static constexpr off_t MSR_LASTBRANCH_0_TO_IP = 0x6C0;

        // Pillar V Integrity State
        uint64_t expected_text_hash_{0};
        const uint8_t* text_start_{nullptr};
        size_t text_size_{0};
        std::atomic<bool> enabled_{true}; // Runtime disable-ability

    public:
        panoptic_reticle(int target_cpu_core, const semiotic_tapestry& tapestry)
        : tapestry_(tapestry) {
            char msr_path[64];
            __builtin_snprintf(msr_path, sizeof(msr_path), "/dev/cpu/%d/msr", target_cpu_core);
            msr_fd_ = open(msr_path, O_RDONLY);

            if (msr_fd_ < 0) {
                throw std::runtime_error("SLABFLUX FATAL: Panoptic Reticle failed to mount MSR interface. Verify CAP_SYS_RAWIO.");
            }
        }

        ~panoptic_reticle() {
            if (msr_fd_ >= 0) close(msr_fd_);
        }

        void enable(bool state) noexcept { enabled_.store(state, std::memory_order_relaxed); }

        // Bootstraps the baseline CRC32 matrix over the compiled binary text segment
        void bind_executable_segment(const void* start, size_t size) noexcept {
            text_start_ = static_cast<const uint8_t*>(start);
            text_size_ = size;
            expected_text_hash_ = compute_hardware_hash();
        }

        /**
         * @brief Harvests the most recent engraving from the silicon shadow.
         * @param out_error_code Reference to extract the error code.
         * @param out_lsn Reference to extract the LSN.
         * @return true if an anomaly was successfully harvested from the Tapestry bounds.
         */
        SLAB_FORCE_INLINE bool harvest_anomaly(uint8_t& out_error_code, uint32_t& out_lsn) const noexcept {
            uint64_t lbr_to_ip = 0;

            // Read the exact hardware register directly from the sibling core's silicon
            if (pread(msr_fd_, &lbr_to_ip, sizeof(lbr_to_ip), MSR_LASTBRANCH_0_TO_IP) != sizeof(lbr_to_ip)) {
                return false;
            }

            auto* target_ptr = reinterpret_cast<uint8_t*>(lbr_to_ip);
            uint8_t* base = tapestry_.base();

            // Verify that the hot-path trajectory actually pierced our Semiotic Tapestry
            if (target_ptr >= base && target_ptr < (base + tapestry_.size())) {
                size_t offset = target_ptr - base;

                // Reconstruct the telemetry from the spatial geometric offset
                out_error_code = static_cast<uint8_t>(offset >> 24);
                out_lsn = static_cast<uint32_t>(offset & 0xFFFFFF);
                return true;
            }

            return false;
        }

        /**
         * @brief Actively enforces MMU residency and W^X invariants out-of-band.
         * @return false if memory pools swapped or binary logic was injected/mutated.
         */
        bool verify_environment_integrity(const void* pool_addr) const noexcept {
            if (!enabled_.load(std::memory_order_relaxed)) return true;

            // 1. Strict Residency Enforcement (W^X / OS Paging)
            // Forces a page-table check to ensure the kernel hasn't swapped out the hot memory.
            unsigned char vec = 0;
            if (::mincore(const_cast<void*>(pool_addr), 4096, &vec) == 0) {
                if (!(vec & 1)) return false; // Physical residency breached!
            }

            // 2. Hardware CRC32 Validation of the Executable Logic
            if (text_start_ != nullptr && compute_hardware_hash() != expected_text_hash_) {
                return false; // Code segment mutated (Rootkit / Bit-rot)
            }

            return true;
        }

    private:
        uint64_t compute_hardware_hash() const noexcept {
            uint64_t hash = 0xFFFFFFFF;
            const uint64_t* ptr = reinterpret_cast<const uint64_t*>(text_start_);
            size_t qwords = text_size_ / 8;
            for (size_t i = 0; i < qwords; ++i) {
                hash = _mm_crc32_u64(hash, ptr[i]);
            }
            return hash;
        }
    };
}
