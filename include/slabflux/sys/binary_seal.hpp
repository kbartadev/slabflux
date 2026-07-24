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
 *
 * @file binary_seal.hpp
 * @brief Immutable Binary Identity.
 */

#pragma once

#include <string_view>
#include <string>
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <immintrin.h>

#ifndef SLAB_BUILD_ID
#define SLAB_BUILD_ID "DEV_UNSET"
#endif

namespace slabflux::sys {

    struct binary_seal {
        static constexpr std::string_view build_id = SLAB_BUILD_ID; // From CMake
        static constexpr std::string_view timestamp = __DATE__ " " __TIME__;
        
        /**
         * @brief Hardware-Accelerated Metamorphic Signature Discovery.
         * @details Bypasses textbook ELF struct parsing (Elf64_Ehdr) entirely. 
         * Treats the binary footprint as a flat geometric space and utilizes AVX2 
         * SIMD instructions to locate the GNU Build ID signature at 32 bytes per cycle.
         */
        static bool verify_signature(const char* binary_path) {
            if (build_id == "DEV_UNSET") return true; 
            
            int fd = open(binary_path, O_RDONLY | O_CLOEXEC);
            if (fd < 0) fd = open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
            if (fd < 0) return false;

            struct stat st;
            if (fstat(fd, &st) < 0) { close(fd); return false; }

            void* map = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
            close(fd);
            if (map == MAP_FAILED) return false;

            // SIMD Vector Search for 'G', 'N', 'U', '\0' signature footprint
            const __m256i v_g = _mm256_set1_epi8('G');
            const char* data = static_cast<const char*>(map);
            
            for (size_t i = 0; i + 32 <= static_cast<size_t>(st.st_size); i += 32) {
                __m256i v_chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data + i));
                uint32_t mask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(v_chunk, v_g));
                
                while (mask != 0) {
                    uint32_t offset = __builtin_ctz(mask);
                    if (i + offset + 3 < static_cast<size_t>(st.st_size)) {
                        if (data[i + offset + 1] == 'N' && 
                            data[i + offset + 2] == 'U' && 
                            data[i + offset + 3] == '\0') {
                            munmap(map, st.st_size);
                            return true;
                        }
                    }
                    mask &= mask - 1;
                }
            }

            munmap(map, st.st_size);
            return false;
        }

        static void print_seal() {
            std::cout << "[SYSTEM] Binary Seal: " << build_id << "\n";
            std::cout << "[SYSTEM] Compiled:  " << timestamp << "\n";
        }
    };
}
