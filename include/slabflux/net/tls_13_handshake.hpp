/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 * @file tls_13_handshake.hpp
 * @brief Zero-Allocation TLS 1.3 Handshake State Machine for QUIC and TCP.
 */

#pragma once
#include <cstdint>
#include <cstddef>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::net {

    /**
     * @brief Zéró-Allokációs TLS 1.3 ClientHello információs blokk.
     * @details Memóriamásolatok helyett kizárólag a dekódolt puffert 
     * célzó mutatókat (offseteket) tárol, így másodpercenként több millió
     * kérést képes parse-olni anélkül, hogy a L1 Cache-t felülírná.
     */
    struct tls_client_hello_info {
        const uint8_t* random{nullptr};
        const uint8_t* session_id{nullptr};
        uint8_t session_id_len{0};
        const uint8_t* cipher_suites{nullptr};
        uint16_t cipher_suites_len{0};
        
        const uint8_t* key_share{nullptr};
        uint16_t key_share_len{0};
        const uint8_t* supported_versions{nullptr};
        uint8_t supported_versions_len{0};
        const uint8_t* sni{nullptr};
        uint16_t sni_len{0};
    };

    class alignas(64) tls_13_handshake_machine {
    public:
        tls_13_handshake_machine() = default;

        /**
         * @brief Natively decodes a TLS 1.3 ClientHello directly from the QUIC CRYPTO frame
         * @return True ha sikeres volt az elemzés, False ha topológiai (hossz) anomália történt.
         */
        SLAB_HOT bool process_client_hello(const uint8_t* crypto_payload, size_t len, tls_client_hello_info& out_info) const noexcept {
            if (SL_EXPECT_FALSE(len < 4)) return false;
            
            uint8_t msg_type = crypto_payload[0];
            uint32_t msg_len = (crypto_payload[1] << 16) | (crypto_payload[2] << 8) | crypto_payload[3];
            
            if (SL_EXPECT_FALSE(msg_type != 1)) return false; // 1 == ClientHello
            if (SL_EXPECT_FALSE(msg_len + 4 > len)) return false; // Payload boundary túllépés
            
            const uint8_t* ptr = crypto_payload + 4;
            const uint8_t* end = ptr + msg_len;
            
            if (SL_EXPECT_FALSE(ptr + 34 > end)) return false; // Legacy version (2) + Random (32)
            ptr += 2; // Skip Legacy Version (TLS 1.2 marker)
            
            out_info.random = ptr;
            ptr += 32;
            
            out_info.session_id_len = *ptr++;
            if (SL_EXPECT_FALSE(ptr + out_info.session_id_len > end)) return false;
            out_info.session_id = ptr;
            ptr += out_info.session_id_len;
            
            if (SL_EXPECT_FALSE(ptr + 2 > end)) return false;
            out_info.cipher_suites_len = (ptr[0] << 8) | ptr[1];
            ptr += 2;
            if (SL_EXPECT_FALSE(ptr + out_info.cipher_suites_len > end)) return false;
            out_info.cipher_suites = ptr;
            ptr += out_info.cipher_suites_len;
            
            if (SL_EXPECT_FALSE(ptr + 1 > end)) return false;
            uint8_t comp_methods_len = *ptr++;
            if (SL_EXPECT_FALSE(ptr + comp_methods_len > end)) return false;
            ptr += comp_methods_len;
            
            if (ptr == end) return true; // Nincsenek kiterjesztések
            
            if (SL_EXPECT_FALSE(ptr + 2 > end)) return false;
            uint16_t ext_total_len = (ptr[0] << 8) | ptr[1];
            ptr += 2;
            if (SL_EXPECT_FALSE(ptr + ext_total_len > end)) return false;
            
            const uint8_t* ext_end = ptr + ext_total_len;
            while (ptr + 4 <= ext_end) {
                uint16_t ext_type = (ptr[0] << 8) | ptr[1];
                uint16_t ext_len = (ptr[2] << 8) | ptr[3];
                ptr += 4;
                if (SL_EXPECT_FALSE(ptr + ext_len > ext_end)) return false;
                
                switch (ext_type) {
                    case 0: // Server Name Indication (SNI)
                        if (ext_len > 5 && ptr[2] == 0) { // 0 == host_name
                            out_info.sni_len = (ptr[3] << 8) | ptr[4];
                            out_info.sni = ptr + 5;
                        }
                        break;
                    case 43: // Supported Versions (TLS 1.3 ellenőrzés)
                        out_info.supported_versions_len = ptr[0];
                        out_info.supported_versions = ptr + 1;
                        break;
                    case 51: // Key Share (ECDHE)
                        out_info.key_share_len = ext_len;
                        out_info.key_share = ptr;
                        break;
                }
                ptr += ext_len;
            }
            
            return true;
        }
    };
} // namespace slabflux::net