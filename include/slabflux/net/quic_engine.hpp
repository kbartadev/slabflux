/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 * @file quic_engine.hpp
 * @brief SlabFlux Zero-Allocation QUIC Multiplexer (RFC 9000).
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/net/virtual_udp_socket.hpp"
#include "slabflux/net/virtual_tls_socket.hpp"
#include "slabflux/net/hkdf_sha256.hpp"
#include "slabflux/net/tls_13_handshake.hpp"
#include "slabflux/core/endian.hpp"
#include "slabflux/core/mpsc_pool.hpp"
#include <atomic>

namespace slabflux::net {

    struct quic_connection_id {
        uint8_t length;
        uint8_t data[20];
        
        bool operator==(const quic_connection_id& o) const noexcept {
            if (length != o.length) return false;
            for (int i = 0; i < length; ++i) {
                if (data[i] != o.data[i]) return false;
            }
            return true;
        }
    };

    // Zero-overhead branchless VarInt Decoder
    static inline uint64_t decode_quic_varint(const uint8_t** ptr, const uint8_t* end) noexcept {
        if (SL_EXPECT_FALSE(*ptr >= end)) return 0;
        uint8_t first = **ptr;
        uint8_t len = 1 << (first >> 6);
        if (SL_EXPECT_FALSE(*ptr + len > end)) return 0;
        uint64_t val = first & 0x3F;
        for (int i = 1; i < len; ++i) {
            val = (val << 8) | (*ptr)[i];
        }
        *ptr += len;
        return val;
    }

    enum class quic_phase : uint8_t {
        CLOSED = 0,
        INITIAL,
        HANDSHAKE,
        ONE_RTT,
        DRAINING,
        CLOSING
    };

    struct alignas(64) quic_transmission_control_block {
        quic_phase phase{quic_phase::CLOSED};
        quic_connection_id local_cid;
        quic_connection_id remote_cid;

        aes_gcm_128_engine rx_initial_aead;
        aes_gcm_128_engine rx_initial_hp;
        aes_gcm_128_engine tx_initial_aead;
        aes_gcm_128_engine tx_initial_hp;
        
        aes_gcm_128_engine rx_app_aead;
        aes_gcm_128_engine rx_app_hp;
        aes_gcm_128_engine tx_app_aead;
        aes_gcm_128_engine tx_app_hp;
        
        uint64_t remote_ip[2]{0, 0};
        uint16_t remote_port{0};
        uint16_t local_port{0};
        
        uint64_t rcv_nxt{0};
        uint64_t snd_nxt{0};
        uint64_t largest_acked{0};
    };

    /**
     * @brief High-speed UDP demultiplexer for QUIC traffic.
     * @details Hooks directly into the `matrix_nexus` UDP handler.
     */
    template <typename GatewayType, size_t MaxConnections = 4096>
    class alignas(64) quic_engine {
        virtual_udp_socket<GatewayType> udp_socket_;
        
        quic_transmission_control_block tcbs_[MaxConnections];

        struct hash_node {
            quic_connection_id cid;
            uint32_t conn_idx;
            std::atomic<hash_node*> next;
        };

        core::mpsc_pool<hash_node, MaxConnections> node_pool_;
        std::atomic<hash_node*> buckets_[MaxConnections]{};
        std::atomic<uint32_t> next_conn_idx_{0};

        static inline uint32_t hash_cid(const quic_connection_id& cid) noexcept {
            uint32_t h = 0x811c9dc5;
            for (int i = 0; i < cid.length; ++i) {
                h ^= cid.data[i];
                h *= 0x01000193;
            }
            return h;
        }

        quic_transmission_control_block* lookup_connection(const quic_connection_id& cid) noexcept {
            uint32_t hash = hash_cid(cid) % MaxConnections;
            hash_node* node = buckets_[hash].load(std::memory_order_relaxed);
            while (node) {
                if (node->cid == cid) return &tcbs_[node->conn_idx];
                node = node->next.load(std::memory_order_relaxed);
            }
            return nullptr;
        }

        quic_transmission_control_block* create_connection(const quic_connection_id& remote_cid, const quic_connection_id& local_cid, const uint64_t src_ip[2], uint16_t src_port) noexcept {
            uint32_t idx = next_conn_idx_.fetch_add(1, std::memory_order_relaxed);
            if (SL_EXPECT_FALSE(idx >= MaxConnections)) return nullptr; // Pool kimerült
            
            hash_node* new_node = node_pool_.make_raw();
            if (SL_EXPECT_FALSE(!new_node)) return nullptr;
            
            new_node->cid = local_cid; 
            new_node->conn_idx = idx;
            new_node->next.store(nullptr, std::memory_order_relaxed);
            
            quic_transmission_control_block& tcb = tcbs_[idx];
            tcb.phase = quic_phase::INITIAL;
            tcb.remote_cid = remote_cid;
            tcb.local_cid = local_cid;
            tcb.remote_ip[0] = src_ip[0]; tcb.remote_ip[1] = src_ip[1];
            tcb.remote_port = src_port;
            tcb.local_port = udp_socket_.get_local_port();
            
            uint32_t hash = hash_cid(local_cid) % MaxConnections;
            hash_node* expected = buckets_[hash].load(std::memory_order_relaxed);
            do {
                new_node->next.store(expected, std::memory_order_relaxed);
            } while (!buckets_[hash].compare_exchange_weak(expected, new_node, std::memory_order_release, std::memory_order_relaxed));
            
            return &tcb;
        }

    public:
        explicit quic_engine(GatewayType& gateway, uint16_t port) noexcept 
            : udp_socket_(gateway, port) {}

        /**
         * @brief Natively parses the first byte of an incoming UDP datagram 
         * to extract QUIC Long/Short Header routing.
         */
        SLAB_HOT void process_inbound_quic(const char* data, size_t len, const uint64_t src_ip[2], uint16_t src_port) noexcept {
            if (SL_EXPECT_FALSE(len < 21)) return; // Too short for QUIC
            
            const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data);
            const uint8_t* end = ptr + len;
            
            uint8_t header_form = *ptr++;
            if (header_form & 0x80) {
                // Long Header (Initial, 0-RTT, Handshake, Retry)
                uint32_t version;
                std::memcpy(&version, ptr, 4);
                version = core::endian::network_to_host32(version);
                ptr += 4;
                if (version == 0) return; // Version Negotiation packet
                
                uint8_t dcid_len = *ptr++;
                if (SL_EXPECT_FALSE(ptr + dcid_len > end)) return;
                
                quic_connection_id dcid{dcid_len, {0}};
                std::memcpy(dcid.data, ptr, dcid_len);
                ptr += dcid_len;
                
                uint8_t scid_len = *ptr++;
                if (SL_EXPECT_FALSE(ptr + scid_len > end)) return;
                
                quic_connection_id scid{scid_len, {0}};
                std::memcpy(scid.data, ptr, scid_len);
                ptr += scid_len; // Skip SCID
                
                quic_transmission_control_block* tcb = lookup_connection(dcid);

                uint8_t packet_type = (header_form & 0x30) >> 4;
                if (packet_type == 0x00) { // Initial Packet (RFC 9000/9001)
                    if (!tcb) {
                        // Szerver oldal: A kapcsolat inicializálása a kliens Initial csomagja alapján
                        tcb = create_connection(scid, dcid, src_ip, src_port);
                        if (SL_EXPECT_FALSE(!tcb)) return; // Megtelt a kapcsolat-pool
                        
                        // RFC 9001: Initial Secrets Derivation
                        static constexpr uint8_t quic_v1_salt[20] = {
                            0x38, 0x76, 0x2c, 0xf7, 0xf5, 0x59, 0x34, 0xb3, 
                            0x4d, 0x17, 0x9a, 0xe6, 0xa4, 0xc8, 0x0c, 0xad, 
                            0xcc, 0xbb, 0x7f, 0x0a
                        };
                        uint8_t initial_secret[32], client_secret[32], server_secret[32];
                        hkdf_sha256::extract(quic_v1_salt, 20, dcid.data, dcid.length, initial_secret);
                        hkdf_sha256::expand_label(initial_secret, "client in", nullptr, 0, client_secret, 32);
                        hkdf_sha256::expand_label(initial_secret, "server in", nullptr, 0, server_secret, 32);
                        
                        uint8_t ck[16], civ[12], chp[16], sk[16], siv[12], shp[16];
                        hkdf_sha256::expand_label(client_secret, "quic key", nullptr, 0, ck, 16);
                        hkdf_sha256::expand_label(client_secret, "quic iv", nullptr, 0, civ, 12);
                        hkdf_sha256::expand_label(client_secret, "quic hp", nullptr, 0, chp, 16);
                        hkdf_sha256::expand_label(server_secret, "quic key", nullptr, 0, sk, 16);
                        hkdf_sha256::expand_label(server_secret, "quic iv", nullptr, 0, siv, 12);
                        hkdf_sha256::expand_label(server_secret, "quic hp", nullptr, 0, shp, 16);
                        
                        tcb->rx_initial_aead.set_key(ck, civ);
                        tcb->rx_initial_hp.set_key(chp, civ);
                        tcb->tx_initial_aead.set_key(sk, siv);
                        tcb->tx_initial_hp.set_key(shp, siv);
                    }

                    uint64_t token_len = decode_quic_varint(&ptr, end);
                    if (SL_EXPECT_FALSE(ptr + token_len > end)) return;
                    ptr += token_len; // Skip Token
                    
                    uint64_t payload_len = decode_quic_varint(&ptr, end);
                    if (SL_EXPECT_FALSE(ptr + payload_len > end)) return;
                    
                    size_t pn_offset = ptr - reinterpret_cast<const uint8_t*>(data);
                    size_t sample_offset = pn_offset + 4; // PN is max 4 bytes
                    if (SL_EXPECT_FALSE(sample_offset + 16 > len)) return;
                    
                    // 1. Remove Header Protection (AES-ECB via encrypt_block)
                    uint8_t hp_mask[16];
                    tcb->rx_initial_hp.encrypt_block(reinterpret_cast<const uint8_t*>(data) + sample_offset, hp_mask);
                    
                    alignas(64) uint8_t packet_buf[1500];
                    std::memcpy(packet_buf, data, len); // L1-resident clone for mutation
                    
                    packet_buf[0] ^= hp_mask[0] & 0x0F; // Unmask Long Header flags
                    uint8_t pn_length = (packet_buf[0] & 0x03) + 1;
                    
                    uint32_t packet_number = 0;
                    for (int i = 0; i < pn_length; ++i) {
                        packet_buf[pn_offset + i] ^= hp_mask[1 + i]; // Unmask PN bytes
                        packet_number = (packet_number << 8) | packet_buf[pn_offset + i];
                    }
                    
                    size_t header_len = pn_offset + pn_length;
                    size_t encrypted_payload_len = payload_len - pn_length;
                    
                    // 2. Decrypt Payload (AEAD AES-128-GCM)
                    alignas(64) uint8_t decrypted_payload[1500];
                    bool valid = tcb->rx_initial_aead.decrypt_quic(
                        packet_buf + header_len, encrypted_payload_len, decrypted_payload,
                        packet_buf, header_len, packet_number
                    );
                    
                    if (SL_EXPECT_TRUE(valid)) {
                        process_quic_frames(*tcb, decrypted_payload, encrypted_payload_len - 16);
                    }
                }
            } else {
                // Short Header (1-RTT Data)
                uint8_t dcid_len = 8; // Assuming standard 8-byte CIDs for internal mesh
                if (SL_EXPECT_FALSE(ptr + dcid_len > end)) return;
                
                quic_connection_id dcid{dcid_len, {0}};
                std::memcpy(dcid.data, ptr, dcid_len);
                ptr += dcid_len;
                
                quic_transmission_control_block* tcb = lookup_connection(dcid);
                if (SL_EXPECT_FALSE(!tcb || tcb->phase != quic_phase::ONE_RTT)) return;

                size_t pn_offset = ptr - reinterpret_cast<const uint8_t*>(data);
                size_t sample_offset = pn_offset + 4;
                if (SL_EXPECT_FALSE(sample_offset + 16 > len)) return;

                // 1. Remove Header Protection (AES-ECB using Application Keys)
                uint8_t hp_mask[16];
                tcb->rx_app_hp.encrypt_block(reinterpret_cast<const uint8_t*>(data) + sample_offset, hp_mask);
                
                alignas(64) uint8_t packet_buf[1500];
                std::memcpy(packet_buf, data, len);
                
                packet_buf[0] ^= hp_mask[0] & 0x1F; // Short header mask uses 0x1F
                uint8_t pn_length = (packet_buf[0] & 0x03) + 1;
                
                uint32_t packet_number = 0;
                for (int i = 0; i < pn_length; ++i) {
                    packet_buf[pn_offset + i] ^= hp_mask[1 + i];
                    packet_number = (packet_number << 8) | packet_buf[pn_offset + i];
                }
                
                size_t header_len = pn_offset + pn_length;
                size_t encrypted_payload_len = len - header_len;
                
                // 2. Decrypt Payload (AEAD AES-128-GCM)
                alignas(64) uint8_t decrypted_payload[1500];
                bool valid = tcb->rx_app_aead.decrypt_quic(
                    packet_buf + header_len, encrypted_payload_len, decrypted_payload,
                    packet_buf, header_len, packet_number
                );
                
                if (SL_EXPECT_TRUE(valid)) {
                    process_quic_frames(*tcb, decrypted_payload, encrypted_payload_len - 16);
                }
            }
        }

        /**
         * @brief Encrypts and transmits outbound QUIC 1-RTT application data natively.
         */
        SLAB_HOT bool send_1rtt_data(quic_transmission_control_block& tcb, const uint8_t* payload, size_t payload_len, uint64_t stream_id) noexcept {
            if (SL_EXPECT_FALSE(tcb.phase != quic_phase::ONE_RTT)) return false;

            alignas(64) uint8_t packet_buf[1500];
            size_t offset = 0;

            // Short Header: 0x40 (Fixed bit) | 0x00 (Spin/Reserved) | KeyPhase(0) | PN_Length(0 = 1 byte)
            packet_buf[offset++] = 0x40;
            
            // Destination Connection ID
            std::memcpy(packet_buf + offset, tcb.remote_cid.data, tcb.remote_cid.length);
            offset += tcb.remote_cid.length;

            size_t pn_offset = offset;
            uint32_t pn = static_cast<uint32_t>(tcb.snd_nxt++);
            packet_buf[offset++] = pn & 0xFF; // 1-byte Packet Number natively mapped
            size_t header_len = offset;

            // Format inner payload: STREAM Frame (0x08 = STREAM, no OFF/LEN limits for a single raw dispatch)
            alignas(64) uint8_t plain_frames[1500];
            size_t pt_offset = 0;
            plain_frames[pt_offset++] = 0x08; // STREAM frame type
            
            if (stream_id < 64) {
                plain_frames[pt_offset++] = static_cast<uint8_t>(stream_id);
            } else {
                plain_frames[pt_offset++] = 0x40 | static_cast<uint8_t>(stream_id >> 8);
                plain_frames[pt_offset++] = static_cast<uint8_t>(stream_id & 0xFF);
            }
            
            std::memcpy(plain_frames + pt_offset, payload, payload_len);
            pt_offset += payload_len;

            // Encrypt Payload with App AEAD Key Schedule
            tcb.tx_app_aead.encrypt_quic(
                plain_frames, pt_offset,
                packet_buf + header_len,
                packet_buf, header_len, pn
            );

            size_t total_packet_len = header_len + pt_offset + 16;

            // Apply Header Protection (HP) masking
            uint8_t hp_mask[16];
            tcb.tx_app_hp.encrypt_block(packet_buf + pn_offset + 4, hp_mask);
            packet_buf[0] ^= hp_mask[0] & 0x1F;
            packet_buf[pn_offset] ^= hp_mask[1];

            // Disgorge to the Gateway
            return udp_socket_.send_to(
                static_cast<uint32_t>(tcb.remote_ip[0]), // Mapped to lower word bounds
                tcb.remote_port, tcb.local_port, 
                reinterpret_cast<const char*>(packet_buf), total_packet_len
            );
        }

    private:
        /**
         * @brief Zero-Allocation parser for decrypted QUIC frames (RFC 9000).
         */
        SLAB_HOT void process_quic_frames(quic_transmission_control_block& tcb, const uint8_t* payload, size_t payload_len) noexcept {
            const uint8_t* ptr = payload;
            const uint8_t* end = payload + payload_len;

            while (ptr < end) {
                uint64_t frame_type = decode_quic_varint(&ptr, end);
                if (SL_EXPECT_FALSE(ptr > end)) break; // Malformed payload boundary

                switch (frame_type) {
                    case 0x00: // PADDING
                        // Fast-forward through continuous padding
                        while (ptr < end && *ptr == 0x00) ptr++;
                        break;

                    case 0x01: // PING
                        // Signal required ACK frame generation for the next outbound UDP flush
                        break;

                    case 0x02: // ACK
                    case 0x03: // ACK with ECN
                    {
                        uint64_t largest_ack = decode_quic_varint(&ptr, end);
                        uint64_t ack_delay = decode_quic_varint(&ptr, end);
                        uint64_t ack_range_count = decode_quic_varint(&ptr, end);
                        uint64_t first_ack_range = decode_quic_varint(&ptr, end);
                        
                        // Skip remaining ACK ranges for basic validation
                        for (uint64_t i = 0; i < ack_range_count; ++i) {
                            decode_quic_varint(&ptr, end); // Gap
                            decode_quic_varint(&ptr, end); // Range Length
                        }

                        if (frame_type == 0x03) {
                            decode_quic_varint(&ptr, end); // ECT(0) Count
                            decode_quic_varint(&ptr, end); // ECT(1) Count
                            decode_quic_varint(&ptr, end); // ECN-CE Count
                        }
                        
                        if (largest_ack > tcb.largest_acked) tcb.largest_acked = largest_ack;
                        break;
                    }

                    case 0x06: // CRYPTO
                    {
                        uint64_t offset = decode_quic_varint(&ptr, end);
                        uint64_t length = decode_quic_varint(&ptr, end);
                        if (SL_EXPECT_FALSE(ptr + length > end)) return; // Malformed Crypto Frame
                        
                        const uint8_t* crypto_data = ptr;
                        ptr += length;

                        if (tcb.phase == quic_phase::INITIAL) {
                            tls_13_handshake_machine tls_machine;
                            tls_client_hello_info client_hello{};
                            
                            if (tls_machine.process_client_hello(crypto_data, length, client_hello)) {
                                // Sikeres O(1) Zero-Allocation dekompozíció!
                                // Itt választhatjuk ki a HKDF-hez (ECDHE) a Server Key-t a 'client_hello.key_share' alapján.
                                // Sikeres egyezkedés esetén -> Lépés a Handshake fázisba!
                                tcb.phase = quic_phase::HANDSHAKE;
                            }
                        }
                        break;
                    }
                    
                    case 0x08: case 0x09: case 0x0A: case 0x0B:
                    case 0x0C: case 0x0D: case 0x0E: case 0x0F: // STREAM
                    {
                        uint64_t stream_id = decode_quic_varint(&ptr, end);
                        uint64_t stream_offset = 0;
                        if (frame_type & 0x04) stream_offset = decode_quic_varint(&ptr, end); // OFF bit
                        uint64_t length = 0;
                        if (frame_type & 0x02) length = decode_quic_varint(&ptr, end); // LEN bit
                        else length = end - ptr; // Remainder of packet

                        if (SL_EXPECT_FALSE(ptr + length > end)) return; // Malformed STREAM Frame
                        const uint8_t* stream_data = ptr;
                        ptr += length;
                        
                        // Demultiplexing to Application logic typically hooks in right here!
                        break;
                    }

                    default:
                        // Unhandled or illegal frame type in this packet context.
                        // RFC 9000: Endpoints MUST treat receipt of a frame in an unsupported packet type as a connection error.
                        return;
                }
            }
        }
    };
} // namespace slabflux::net