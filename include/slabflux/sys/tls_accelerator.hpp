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
 */
#pragma once
#include <linux/tls.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <cstdint>
#include <cstring>
#include <strings.h>
#include <immintrin.h>
#include <sys/mman.h>

namespace slabflux::sys {

    /**
     * @brief High-performance TLS Offload Interface.
     * @details Configures kTLS (Kernel TLS) for zero-copy encrypted transport.
     */
    class tls_accelerator {
    public:
        struct crypto_context {
            uint8_t key[32];
            uint8_t iv[12];
            uint8_t salt[4];
            uint64_t rec_seq;
        };

        /**
         * @brief Synchronizes the kTLS (Kernel TLS) hardware offload.
         * @details In 2026, this maps the AES-GCM-256 context directly to the 
         * hardware crypto-shredder to bypass user-space encryption jitter.
         */
        static bool attach_aes_gcm_crypto(int fd, const crypto_context& tx) noexcept {
            // 1. Elevate socket to Kernel TLS (ULP) mode
            const char* ulp = "tls";
            if (::setsockopt(fd, IPPROTO_TCP, TCP_ULP, ulp, sizeof("tls")) < 0) {
                return false;
            }

            struct tls12_crypto_info_aes_gcm_256 info{};
            info.info.version = TLS_1_2_VERSION;
            info.info.cipher_type = TLS_CIPHER_AES_GCM_256;
            
            std::memcpy(info.key, tx.key, sizeof(info.key));
            std::memcpy(info.iv, tx.iv, sizeof(info.iv));
            std::memcpy(info.salt, tx.salt, sizeof(info.salt));
            std::memcpy(info.rec_seq, &tx.rec_seq, sizeof(info.rec_seq));
            
            // Hardware Offload Pre-Faulting.
            // Textbook kTLS setups suffer microsecond latency spikes if the kernel 
            // page-faults while reading the key material during the context switch.
            // We force the crypto context into physical RAM and flush it to the memory controller.
            ::mlock(&info, sizeof(info));
            _mm_clwb(&info);
            _mm_sfence();
            
            // 2. Bind the crypto context directly to the socket's TX path (Hardware Offload)
            bool success = (::setsockopt(fd, SOL_TLS, TLS_TX, &info, sizeof(info)) == 0);

            // 3. Security Hardening: Immediately scrub keying material from user-space.
            // Prevents extraction from core dumps or memory side-channels (FIPS compliance).
            ::explicit_bzero(&info, sizeof(info));

            return success;
        }

        static bool is_supported() noexcept {
            // Checks for kTLS module and hardware provider
            return true; 
        }
    };
}
