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
 * ============================================================================* @file tls_record_layer.hpp
 * @brief Pipeline Middlebox connecting L4 TCP to L7 Parsers.
 */

#pragma once
#include "tls_record_extractor.hpp"
#include "aes_gcm_hardware.hpp"
#include "slabflux/platform/os.hpp"
#include "slabflux/net/tcp_gateway.hpp" // For inbound_stream_frame
#include "slabflux/net/virtual_tcp_socket.hpp"
#include "slabflux/security/tls_handshake_machine.hpp"

namespace slabflux::security {

    struct alignas(128) tls_session_block {
        tls_session_keys keys;
        tls_handshake_context hs_ctx;
        
        // Native fragmentation buffer for TCP segment boundaries
        uint32_t rx_buffer_len{0};
        alignas(64) char rx_buffer[16384 + 256];
    };

    template <std::size_t MaxConnections = 1048576>
    class alignas(64) tls_crypto_registry {
        tls_session_block* sessions_{nullptr};
        std::size_t pool_size_{0};

    public:
        tls_crypto_registry() {
            pool_size_ = MaxConnections * sizeof(tls_session_block);
            // Pre-allocate 1M keys in HugePages to prevent TLB misses on key lookups
            int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_HUGETLB | MAP_HUGE_2MB;
            void* mem = ::mmap(nullptr, pool_size_, PROT_READ | PROT_WRITE, flags, -1, 0);
            if (mem == MAP_FAILED) {
                flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE;
                mem = ::mmap(nullptr, pool_size_, PROT_READ | PROT_WRITE, flags, -1, 0);
                if (mem == MAP_FAILED) throw std::bad_alloc();
            }
            sessions_ = static_cast<tls_session_block*>(mem);
        }

        ~tls_crypto_registry() { if (sessions_) ::munmap(sessions_, pool_size_); }

        SLAB_FORCE_INLINE tls_session_block& get_session(uint32_t conn_id) noexcept {
            return sessions_[conn_id % MaxConnections];
        }
    };

    template <typename PlaintextDefragmenter>
    class alignas(64) tls_record_layer {
        tls_crypto_registry<>& registry_;
        PlaintextDefragmenter& downstream_l7_;

        net::virtual_tcp_socket* bound_socket_{nullptr};

    public:
        tls_record_layer(tls_crypto_registry<>& reg, PlaintextDefragmenter& downstream) 
            : registry_(reg), downstream_l7_(downstream) {}

        void bind_socket(net::virtual_tcp_socket* sock) noexcept { bound_socket_ = sock; }

        SLAB_HOT void on_temporal(uint32_t conn_id, uint64_t current_time_ms, net::tcp_transmission_control_block& tcb) noexcept {
            tls_session_block& session = registry_.get_session(conn_id);
            
            if (SL_EXPECT_TRUE(session.hs_ctx.state == tls_handshake_state::ESTABLISHED)) {
                if (session.hs_ctx.is_server) {
                    // Enforce hourly NewSessionTicket rotation
                    if (SL_EXPECT_FALSE(current_time_ms - session.hs_ctx.last_ticket_ms >= 3600000)) { 
                        session.hs_ctx.last_ticket_ms = current_time_ms;

                        alignas(64) char egress_scratch[1024];
                        size_t written = tls_handshake_machine::generate_new_session_ticket(
                            session.keys, session.hs_ctx, egress_scratch, sizeof(egress_scratch)
                        );

                        if (written > 0) {
                            // Create a zero-allocation virtual socket to wrap the TLS buffer inside a native TCP segment
                            net::virtual_tcp_socket sock(&tcb);
                            sock.send(egress_scratch, written);
                        }
                    }
                }

                // Enforce KeyUpdate rotation (e.g., every 24 hours)
                if (SL_EXPECT_FALSE(current_time_ms - session.hs_ctx.last_key_update_ms >= 86400000)) { 
                    session.hs_ctx.last_key_update_ms = current_time_ms;

                    alignas(64) char egress_scratch[1024];
                    size_t written = tls_handshake_machine::generate_key_update(
                        session.keys, session.hs_ctx, egress_scratch, sizeof(egress_scratch), 1 // update_requested
                    );

                    if (written > 0) {
                        net::virtual_tcp_socket sock(&tcb);
                        sock.send(egress_scratch, written);
                    }
                }
            }

            // Periodically scavenge and sweep the global ticket vault (e.g. every 10 seconds)
            static uint64_t last_vault_sweep = 0;
            if (current_time_ms - last_vault_sweep > 10000) {
                tls_ticket_vault::scavenge();
                tls_ticket_vault::sweep_expired();
                last_vault_sweep = current_time_ms;
            }

            // Forward temporal sweep to downstream application logic if supported
            if constexpr (requires { downstream_l7_.on_temporal(conn_id, current_time_ms, tcb); }) {
                downstream_l7_.on_temporal(conn_id, current_time_ms, tcb);
            }
        }

        SLAB_HOT bool process_record(tls_record_event& ev) noexcept {
            tls_session_block& session = registry_.get_session(ev.connection_id);

            // Route Handshake (0x16 / 22) to the State Machine
            if (SL_EXPECT_FALSE(ev.content_type == 22)) { 
                if (bound_socket_) {
                    session.hs_ctx.remote_ipv4 = bound_socket_->get_remote_ipv4();
                    auto psk = bound_socket_->get_pending_psk();
                    if (!psk.ticket.empty()) {
                        tls_ticket_vault::store(session.hs_ctx.remote_ipv4, psk.lifetime, psk.age_add, psk.ticket);
                        bound_socket_->clear_pending_psk();
                    }
                }

                alignas(64) char egress_scratch[4096];
                size_t written = tls_handshake_machine::process_handshake(
                    session.keys, 
                    session.hs_ctx, 
                    ev.ciphertext, 
                    egress_scratch,
                    sizeof(egress_scratch)
                );
                if (SL_EXPECT_FALSE(session.hs_ctx.state == tls_handshake_state::FAILED)) {
                    if (bound_socket_) bound_socket_->close(); // Forcefully kick unauthorized connections via the Gateway
                } else if (written > 0 && bound_socket_) {
                    bound_socket_->send(egress_scratch, written);
                }
                return false;
            }

            // Only process Application Data (0x17 / 23) in the hot path.
            if (SL_EXPECT_FALSE(ev.content_type != 23)) return false;
            if (SL_EXPECT_FALSE(!session.keys.active)) return false; // Handshake not complete

            // ZERO-ALLOCATION IN-PLACE DECRYPTION
            // Since ev.ciphertext points into the TCP stream buffer (which is mutable),
            // we decrypt it over itself, transforming the ciphertext into plaintext instantly.
            char* in_out_target = const_cast<char*>(ev.ciphertext.data());
            
            size_t plaintext_len = aes_gcm_hardware::decrypt_in_place(
                session.keys.client_write_key, 
                session.keys.client_write_iv, 
                session.keys.client_seq++, 
                in_out_target, 
                ev.ciphertext.size()
            );

            if (SL_EXPECT_FALSE(plaintext_len == 0)) return false; // Cryptographic Forgery Detected!

            // TLS 1.3 Inner Content Type stripping (The actual type is buried at the end of the plaintext)
            uint8_t inner_type = static_cast<uint8_t>(in_out_target[plaintext_len - 1]);
            if (SL_EXPECT_TRUE(inner_type == 23)) { // Application Data
                net::tcp_gateway<PlaintextDefragmenter>::inbound_stream_frame pt_frame{in_out_target, plaintext_len - 1, ev.connection_id};
                return downstream_l7_.on(pt_frame);
            } else if (SL_EXPECT_FALSE(inner_type == 22)) { // Encrypted Handshake
                // Re-route decrypted ServerHello flight (EncryptedExtensions, Cert, Finished) 
                // back into the Handshake Machine using the same buffer for in-place generation.
                alignas(64) char egress_scratch[4096];
                size_t written = tls_handshake_machine::process_handshake(
                    session.keys, 
                    session.hs_ctx, 
                    std::string_view(in_out_target, plaintext_len - 1), 
                    egress_scratch, 
                    sizeof(egress_scratch)
                );
                if (SL_EXPECT_FALSE(session.hs_ctx.state == tls_handshake_state::FAILED)) {
                    if (bound_socket_) bound_socket_->close(); // Trap EncryptedExtensions validation failures
                } else if (written > 0 && bound_socket_) {
                    bound_socket_->send(egress_scratch, written);
                }
            }
            return false;
        }

        /**
         * @brief TCP Byte-Stream Ingress Bridge.
         * @details Buffers and seamlessly defragments TLS records split across TCP MTU boundaries.
         */
        SLAB_HOT bool on(const net::tcp_gateway<PlaintextDefragmenter>::inbound_stream_frame& frame) noexcept {
            tls_session_block& session = registry_.get_session(frame.connection_id);
            
            size_t available = sizeof(session.rx_buffer) - session.rx_buffer_len;
            size_t to_copy = std::min(frame.payload_length, available);
            if (to_copy == 0) return false; // Buffer overflow guard
            
            __builtin_memcpy(session.rx_buffer + session.rx_buffer_len, frame.data, to_copy);
            session.rx_buffer_len += static_cast<uint32_t>(to_copy);
            
            size_t offset = 0;
            bool processed = false;
            
            while (offset < session.rx_buffer_len) {
                tls_record_event ev;
                ev.connection_id = frame.connection_id;
                
                auto status = tls_record_extractor::parse(std::string_view(session.rx_buffer + offset, session.rx_buffer_len - offset), ev);
                
                if (status == transport::parser_status::OK) {
                    processed |= process_record(ev);
                    offset += ev.total_bytes_consumed;
                } else if (status == transport::parser_status::INCOMPLETE) {
                    break; // Wait for the next TCP segment to complete the TLS record
                } else {
                    if (bound_socket_) bound_socket_->close(); // Malformed Record
                    return false;
                }
            }
            
            if (offset > 0) {
                size_t remaining = session.rx_buffer_len - offset;
                if (remaining > 0) {
                    __builtin_memmove(session.rx_buffer, session.rx_buffer + offset, remaining);
                }
                session.rx_buffer_len = static_cast<uint32_t>(remaining);
            }
            return processed;
        }
    };

} // namespace slabflux::security