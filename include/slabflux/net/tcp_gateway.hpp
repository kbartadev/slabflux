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
 * ============================================================================* @file tcp_gateway.hpp
 * @brief Decoupled routing from Nexus I/O -> TCP Flow Engine -> Upper Protocol.
 */

#pragma once

#include "slabflux/net/tcp_flow_engine.hpp"
#include "slabflux/net/virtual_tcp_socket.hpp"
#include "slabflux/net/raw_tcp_ipv4_frame.hpp"
#include "slabflux/net/raw_udp_ipv6_frame.hpp"
#include "slabflux/core/endian.hpp"
#include "slabflux/net/tcp_spatial_ooo_matrix.hpp"
#include "slabflux/net/tcp_temporal_wheel.hpp"
#include "slabflux/net/tcp_syn_cookie.hpp"
#include "slabflux/net/tcp_wire_engine.hpp"
#include "slabflux/core/mpsc_pool.hpp"
#include "slabflux/net/raw_udp_ipv4_frame.hpp"
#include <iostream>

#ifndef SLAB_GW_DEBUG
#define SLAB_GW_DEBUG(msg) std::cerr << "[GW DROP " << __LINE__ << "] " << msg << std::endl
#define GW_DROP(msg) do { SLAB_GW_DEBUG(msg); return; } while(0)
#define GW_DROP_FALSE(msg) do { SLAB_GW_DEBUG(msg); return false; } while(0)
#endif

namespace slabflux::net {

    template<typename Defragmenter>
    class alignas(core::CACHE_LINE_SIZE) tcp_gateway {
        Defragmenter& defragmenter_;
        
        // Fixed connection mapping table (Zero-allocation)
        tcp_transmission_control_block tcbs_[1024]; 

        // Internal mesh OOO Matrix Pool for handling local switch microbursts
        core::mpsc_pool<tcp_spatial_ooo_matrix, 256> ooo_pool_;
        
        // Gateway-level conduits to prevent stateless coupling failures
        core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024>* gateway_egress_{nullptr};
        core::spsc_ring_conduit<uint32_t, 1024>* accept_queue_{nullptr};
        struct rte_mempool* tx_mbuf_pool_{nullptr};

        struct mbuf_cleanup_guard {
            struct rte_mbuf*& ptr;
            bool absorbed{false};
            ~mbuf_cleanup_guard() {
                if (ptr && !absorbed) rte_pktmbuf_free(ptr);
            }
        };

    public:
        struct inbound_stream_frame {
            const char* data;
            std::size_t payload_length;
            std::size_t connection_id;
        };

        struct inbound_udp_frame {
            const char* data;
            std::size_t payload_length;
            uint32_t src_ip;
            uint32_t dst_ip;
            uint16_t src_port;
            uint16_t dst_port;
        };

        struct inbound_udp_ipv6_frame {
            const char* data;
            std::size_t payload_length;
            uint64_t src_ipv6[2];
            uint64_t dst_ipv6[2];
            uint16_t src_port;
            uint16_t dst_port;
        };

    public:
        explicit tcp_gateway(Defragmenter& defrag) noexcept : defragmenter_(defrag) {}

        void bind_conduits(core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024>* egress,
                           core::spsc_ring_conduit<uint32_t, 1024>* accept) noexcept {
            gateway_egress_ = egress;
            accept_queue_ = accept;
        }

        void bind_mempool(struct rte_mempool* pool) noexcept {
            tx_mbuf_pool_ = pool;
        }

        // Expose TCB for diagnostic and testing integrations
        SLAB_FORCE_INLINE tcp_transmission_control_block& get_tcb(size_t conn_id) noexcept {
            return tcbs_[conn_id & 1023];
        }

        SLAB_FORCE_INLINE void stateless_syn_ack(raw_tcp_ipv4_frame& out_hdr, const raw_tcp_ipv4_frame& in_hdr, uint32_t cookie, uint64_t current_time_ms, uint8_t wscale, uint8_t sack, bool ts, uint32_t client_tsval) noexcept {
            // L2 Reverse
            __builtin_memcpy(out_hdr.dest_mac, in_hdr.src_mac, 6);
            __builtin_memcpy(out_hdr.src_mac, in_hdr.dest_mac, 6);
            out_hdr.eth_type = in_hdr.eth_type;

            uint8_t opt_len = 12;
            if (ts) opt_len += 12;

            // L3 Reverse
            out_hdr.ip_ihl_ver = 0x45;
            out_hdr.ip_tos = 0;
            out_hdr.ip_len = core::endian::network_to_host16(40 + opt_len);
            out_hdr.ip_id = 0;
            out_hdr.ip_frag_offset = core::endian::network_to_host16(0x4000);
            out_hdr.ip_ttl = 64;
            out_hdr.ip_protocol = 6;
            out_hdr.ip_src = in_hdr.ip_dst;
            out_hdr.ip_dst = in_hdr.ip_src;
            out_hdr.ip_checksum = 0;
            out_hdr.ip_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(&out_hdr) + 14, 20, 0);

            // L4 Reverse
            out_hdr.tcp_src_port = in_hdr.tcp_dst_port;
            out_hdr.tcp_dst_port = in_hdr.tcp_src_port;
            out_hdr.tcp_seq = core::endian::network_to_host32(cookie);
            out_hdr.tcp_ack = core::endian::network_to_host32(core::endian::network_to_host32(in_hdr.tcp_seq) + 1);
            out_hdr.tcp_data_offset = ((20 + opt_len) / 4) << 4; 
            if ((in_hdr.tcp_flags & (FLAG_ECE | FLAG_CWR)) == (FLAG_ECE | FLAG_CWR)) {
                out_hdr.tcp_flags = FLAG_SYN | FLAG_ACK | FLAG_ECE;
            } else {
                out_hdr.tcp_flags = FLAG_SYN | FLAG_ACK;
            }
            out_hdr.tcp_window = core::endian::network_to_host16(65535);
            out_hdr.tcp_urgent_ptr = 0;
            
            out_hdr.tcp_options[0] = 0x02; out_hdr.tcp_options[1] = 0x04;
            out_hdr.tcp_options[2] = 0x05; out_hdr.tcp_options[3] = 0xB4; // MSS 1460
            out_hdr.tcp_options[4] = 0x01; // NOP
            out_hdr.tcp_options[5] = 0x03; out_hdr.tcp_options[6] = 0x03;
            out_hdr.tcp_options[7] = 0x07; // WScale 7
            out_hdr.tcp_options[8] = 0x04; out_hdr.tcp_options[9] = 0x02; // SACK Permitted
            out_hdr.tcp_options[10] = 0x01; out_hdr.tcp_options[11] = 0x01; // NOP NOP

            if (ts) {
                out_hdr.tcp_options[12] = 0x01; out_hdr.tcp_options[13] = 0x01;
                out_hdr.tcp_options[14] = 0x08; out_hdr.tcp_options[15] = 0x0A;
                uint32_t tsval = (static_cast<uint32_t>(current_time_ms) & ~0x1F) | (wscale << 1) | sack;
                *reinterpret_cast<uint32_t*>(&out_hdr.tcp_options[16]) = core::endian::host_to_network32(tsval);
                *reinterpret_cast<uint32_t*>(&out_hdr.tcp_options[20]) = core::endian::host_to_network32(client_tsval);
            }

            uint16_t tcp_len = 20 + opt_len;
            uint32_t pseudo_sum = 0;
            uint32_t src = out_hdr.ip_src;
            uint32_t dst = out_hdr.ip_dst;
            pseudo_sum += (src & 0xFFFF) + (src >> 16);
            pseudo_sum += (dst & 0xFFFF) + (dst >> 16);
            pseudo_sum += core::endian::host_to_network16(6);
            pseudo_sum += core::endian::host_to_network16(tcp_len);
            out_hdr.tcp_checksum = 0;
            out_hdr.tcp_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(&out_hdr) + 34, tcp_len, pseudo_sum);
        }

        SLAB_FORCE_INLINE void stateless_syn_ack_ipv6(raw_tcp_ipv6_frame& out_hdr, const raw_tcp_ipv6_frame& in_hdr, uint32_t cookie, uint64_t current_time_ms, uint8_t wscale, uint8_t sack, bool ts, uint32_t client_tsval) noexcept {
            // L2 Reverse
            __builtin_memcpy(out_hdr.dest_mac, in_hdr.src_mac, 6);
            __builtin_memcpy(out_hdr.src_mac, in_hdr.dest_mac, 6);
            out_hdr.eth_type = in_hdr.eth_type;

            uint8_t opt_len = 12;
            if (ts) opt_len += 12;

            // L3 Reverse
            out_hdr.ipv6_flow = core::endian::host_to_network32(0x60000000);
            out_hdr.ipv6_plen = core::endian::host_to_network16(20 + opt_len);
            out_hdr.ipv6_nxt = 6;
            out_hdr.ipv6_hlim = 64;
            out_hdr.ipv6_src[0] = in_hdr.ipv6_dst[0];
            out_hdr.ipv6_src[1] = in_hdr.ipv6_dst[1];
            out_hdr.ipv6_dst[0] = in_hdr.ipv6_src[0];
            out_hdr.ipv6_dst[1] = in_hdr.ipv6_src[1];

            // L4 Reverse
            out_hdr.tcp_src_port = in_hdr.tcp_dst_port;
            out_hdr.tcp_dst_port = in_hdr.tcp_src_port;
            out_hdr.tcp_seq = core::endian::network_to_host32(cookie);
            out_hdr.tcp_ack = core::endian::network_to_host32(core::endian::network_to_host32(in_hdr.tcp_seq) + 1);
            out_hdr.tcp_data_offset = ((20 + opt_len) / 4) << 4; 
            if ((in_hdr.tcp_flags & (FLAG_ECE | FLAG_CWR)) == (FLAG_ECE | FLAG_CWR)) {
                out_hdr.tcp_flags = FLAG_SYN | FLAG_ACK | FLAG_ECE;
            } else {
                out_hdr.tcp_flags = FLAG_SYN | FLAG_ACK;
            }
            out_hdr.tcp_window = core::endian::network_to_host16(65535);
            out_hdr.tcp_urgent_ptr = 0;
            
            out_hdr.tcp_options[0] = 0x02; out_hdr.tcp_options[1] = 0x04;
            out_hdr.tcp_options[2] = 0x05; out_hdr.tcp_options[3] = 0xB4; // MSS 1460
            out_hdr.tcp_options[4] = 0x01; // NOP
            out_hdr.tcp_options[5] = 0x03; out_hdr.tcp_options[6] = 0x03;
            out_hdr.tcp_options[7] = 0x07; // WScale 7
            out_hdr.tcp_options[8] = 0x04; out_hdr.tcp_options[9] = 0x02; // SACK Permitted
            out_hdr.tcp_options[10] = 0x01; out_hdr.tcp_options[11] = 0x01; // NOP NOP

            if (ts) {
                out_hdr.tcp_options[12] = 0x01; out_hdr.tcp_options[13] = 0x01;
                out_hdr.tcp_options[14] = 0x08; out_hdr.tcp_options[15] = 0x0A;
                uint32_t tsval = (static_cast<uint32_t>(current_time_ms) & ~0x1F) | (wscale << 1) | sack;
                *reinterpret_cast<uint32_t*>(&out_hdr.tcp_options[16]) = core::endian::host_to_network32(tsval);
                *reinterpret_cast<uint32_t*>(&out_hdr.tcp_options[20]) = core::endian::host_to_network32(client_tsval);
            }

            uint16_t tcp_len = 20 + opt_len;
            uint32_t pseudo_sum = 0;
            const uint16_t* src16 = reinterpret_cast<const uint16_t*>(out_hdr.ipv6_src);
            const uint16_t* dst16 = reinterpret_cast<const uint16_t*>(out_hdr.ipv6_dst);
            for(int i=0; i<8; ++i) pseudo_sum += src16[i];
            for(int i=0; i<8; ++i) pseudo_sum += dst16[i];
            pseudo_sum += core::endian::host_to_network16(6);
            pseudo_sum += core::endian::host_to_network16(tcp_len);
            out_hdr.tcp_checksum = 0;
            out_hdr.tcp_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(&out_hdr) + 54, tcp_len, pseudo_sum);
        }

        SLAB_FORCE_INLINE void stateless_rst_ack(raw_tcp_ipv4_frame& out_hdr, const raw_tcp_ipv4_frame& in_hdr) noexcept {
            __builtin_memcpy(out_hdr.dest_mac, in_hdr.src_mac, 6);
            __builtin_memcpy(out_hdr.src_mac, in_hdr.dest_mac, 6);
            out_hdr.eth_type = in_hdr.eth_type;
            out_hdr.ip_ihl_ver = 0x45;
            out_hdr.ip_tos = 0;
            out_hdr.ip_len = core::endian::network_to_host16(40);
            out_hdr.ip_id = 0;
            out_hdr.ip_frag_offset = core::endian::network_to_host16(0x4000);
            out_hdr.ip_ttl = 64;
            out_hdr.ip_protocol = 6;
            out_hdr.ip_src = in_hdr.ip_dst;
            out_hdr.ip_dst = in_hdr.ip_src;
            out_hdr.ip_checksum = 0;
            out_hdr.ip_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(&out_hdr) + 14, 20, 0);
            out_hdr.tcp_src_port = in_hdr.tcp_dst_port;
            out_hdr.tcp_dst_port = in_hdr.tcp_src_port;
            
            if (in_hdr.tcp_flags & FLAG_ACK) {
                out_hdr.tcp_seq = in_hdr.tcp_ack;
                out_hdr.tcp_ack = 0;
                out_hdr.tcp_flags = FLAG_RST;
            } else {
                out_hdr.tcp_seq = 0;
                uint32_t in_seq = core::endian::network_to_host32(in_hdr.tcp_seq);
                uint32_t header_words = (in_hdr.ip_ihl_ver & 0x0F) + (in_hdr.tcp_data_offset >> 4);
                uint32_t payload_len = core::endian::network_to_host16(in_hdr.ip_len) - (header_words * 4);
                out_hdr.tcp_ack = core::endian::host_to_network32(in_seq + payload_len + ((in_hdr.tcp_flags & (FLAG_SYN | FLAG_FIN)) ? 1 : 0));
                out_hdr.tcp_flags = FLAG_RST | FLAG_ACK;
            }
            
            out_hdr.tcp_data_offset = 0x50; 
            out_hdr.tcp_window = 0;
            out_hdr.tcp_urgent_ptr = 0;
            
            uint16_t tcp_len = 20;
            uint32_t pseudo_sum = 0;
            uint32_t src = out_hdr.ip_src;
            uint32_t dst = out_hdr.ip_dst;
            pseudo_sum += (src & 0xFFFF) + (src >> 16);
            pseudo_sum += (dst & 0xFFFF) + (dst >> 16);
            pseudo_sum += core::endian::host_to_network16(6);
            pseudo_sum += core::endian::host_to_network16(tcp_len);
            out_hdr.tcp_checksum = 0;
            out_hdr.tcp_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(&out_hdr) + 34, tcp_len, pseudo_sum);
        }

        SLAB_FORCE_INLINE void stateless_rst_ack_ipv6(raw_tcp_ipv6_frame& out_hdr, const raw_tcp_ipv6_frame& in_hdr) noexcept {
            __builtin_memcpy(out_hdr.dest_mac, in_hdr.src_mac, 6);
            __builtin_memcpy(out_hdr.src_mac, in_hdr.dest_mac, 6);
            out_hdr.eth_type = in_hdr.eth_type;
            out_hdr.ipv6_flow = core::endian::host_to_network32(0x60000000);
            out_hdr.ipv6_plen = core::endian::host_to_network16(20);
            out_hdr.ipv6_nxt = 6;
            out_hdr.ipv6_hlim = 64;
            out_hdr.ipv6_src[0] = in_hdr.ipv6_dst[0];
            out_hdr.ipv6_src[1] = in_hdr.ipv6_dst[1];
            out_hdr.ipv6_dst[0] = in_hdr.ipv6_src[0];
            out_hdr.ipv6_dst[1] = in_hdr.ipv6_src[1];
            out_hdr.tcp_src_port = in_hdr.tcp_dst_port;
            out_hdr.tcp_dst_port = in_hdr.tcp_src_port;
            
            if (in_hdr.tcp_flags & FLAG_ACK) {
                out_hdr.tcp_seq = in_hdr.tcp_ack;
                out_hdr.tcp_ack = 0;
                out_hdr.tcp_flags = FLAG_RST;
            } else {
                out_hdr.tcp_seq = 0;
                uint32_t in_seq = core::endian::network_to_host32(in_hdr.tcp_seq);
                uint32_t header_words = (in_hdr.tcp_data_offset >> 4);
                uint32_t payload_len = core::endian::network_to_host16(in_hdr.ipv6_plen) - (header_words * 4);
                out_hdr.tcp_ack = core::endian::host_to_network32(in_seq + payload_len + ((in_hdr.tcp_flags & (FLAG_SYN | FLAG_FIN)) ? 1 : 0));
                out_hdr.tcp_flags = FLAG_RST | FLAG_ACK;
            }
            
            out_hdr.tcp_data_offset = 0x50; 
            out_hdr.tcp_window = 0;
            out_hdr.tcp_urgent_ptr = 0;
            
            uint16_t tcp_len = 20;
            uint32_t pseudo_sum = 0;
            const uint16_t* src16 = reinterpret_cast<const uint16_t*>(out_hdr.ipv6_src);
            const uint16_t* dst16 = reinterpret_cast<const uint16_t*>(out_hdr.ipv6_dst);
            for(int i=0; i<8; ++i) pseudo_sum += src16[i];
            for(int i=0; i<8; ++i) pseudo_sum += dst16[i];
            pseudo_sum += core::endian::host_to_network16(6);
            pseudo_sum += core::endian::host_to_network16(tcp_len);
            out_hdr.tcp_checksum = 0;
            out_hdr.tcp_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(&out_hdr) + 54, tcp_len, pseudo_sum);
        }
        
        /**
         * @brief Natively injects a stateless UDP datagram onto the internal mesh wire.
         * @details Requires explicit MACs/IPs as the internal mesh operates statelessly and without an ARP cache.
         * @return True if enqueued, False if dropped (e.g. queue full).
         */
        SLAB_HOT bool send_udp(const uint8_t* target_mac, const uint8_t* source_mac, uint32_t target_ip, uint32_t source_ip, uint16_t target_port, uint16_t source_port, const char* payload, size_t payload_len) noexcept {
            // CRITICAL FIX: Prevent DPDK MBUF memory corruption & Ethernet Giant Frame drops
            // Standard Ethernet MTU (1500) - IPv4 (20) - UDP (8) = 1472 bytes max payload.
            if (SL_EXPECT_FALSE(payload_len > 1472)) return false;

            // CRITICAL FIX: L7 Null Pointer Segfault (Kernel copy_from_user trap equivalent)
            if (SL_EXPECT_FALSE(payload == nullptr && payload_len > 0)) return false;

            if (SL_EXPECT_FALSE(!gateway_egress_ || !tx_mbuf_pool_)) return false;

            auto* slot = gateway_egress_->get_reserved_slot(0);
            if (SL_EXPECT_FALSE(!slot)) return false;

            slot->mbuf = rte_pktmbuf_alloc(tx_mbuf_pool_);
            if (SL_EXPECT_FALSE(!slot->mbuf)) return false;

            char* out_data = rte_pktmbuf_mtod(slot->mbuf, char*);
            auto* out_hdr = reinterpret_cast<raw_udp_ipv4_frame*>(out_data);

            __builtin_memcpy(out_hdr->dest_mac, target_mac, 6);
            __builtin_memcpy(out_hdr->src_mac, source_mac, 6);
            out_hdr->eth_type = core::endian::host_to_network16(0x0800);

            uint16_t ip_len = static_cast<uint16_t>(20 + 8 + payload_len);
            out_hdr->ip_ihl_ver = 0x45;
            out_hdr->ip_tos = 0;
            out_hdr->ip_len = core::endian::host_to_network16(ip_len);
            out_hdr->ip_id = 0;
            out_hdr->ip_frag_offset = core::endian::host_to_network16(0x4000); // DF
            out_hdr->ip_ttl = 64;
            out_hdr->ip_protocol = 17; // UDP
            out_hdr->ip_src = source_ip;
            out_hdr->ip_dst = target_ip;
            out_hdr->ip_checksum = 0;
            out_hdr->ip_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(out_hdr) + 14, 20, 0);

            out_hdr->udp_src_port = core::endian::host_to_network16(source_port);
            out_hdr->udp_dst_port = core::endian::host_to_network16(target_port);
            out_hdr->udp_length = core::endian::host_to_network16(8 + payload_len);
            out_hdr->udp_checksum = 0;

            if (payload_len > 0) {
                std::memcpy(out_data + 42, payload, payload_len);
            }

            uint32_t pseudo_sum = 0;
            uint32_t src = out_hdr->ip_src;
            uint32_t dst = out_hdr->ip_dst;
            pseudo_sum += (src & 0xFFFF) + (src >> 16);
            pseudo_sum += (dst & 0xFFFF) + (dst >> 16);
            pseudo_sum += core::endian::host_to_network16(17);
            pseudo_sum += out_hdr->udp_length;
            
            uint16_t csum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(out_hdr) + 34, 8 + payload_len, pseudo_sum);
            out_hdr->udp_checksum = (csum == 0) ? 0xFFFF : csum; // RFC 768 Exception

            uint32_t final_len = 14 + ip_len;
            if (SL_EXPECT_FALSE(final_len < 60)) {
                std::memset(out_data + final_len, 0, 60 - final_len);
                final_len = 60;
            }
            slot->mbuf->data_len = final_len;
            slot->mbuf->pkt_len = final_len;
            
            gateway_egress_->commit_n(1);
            return true;
        }

        /**
         * @brief Evaluates an incoming physical Layer-2 frame.
         * @details Fits seamlessly into `matrix_nexus::poll_and_execute` loop as the target logic.
         */
        SLAB_HOT void on_raw_frame(const char* data, size_t length, size_t conn_id, struct rte_mbuf* mbuf = nullptr) noexcept {
            mbuf_cleanup_guard mbuf_guard{mbuf};

            if (SL_EXPECT_FALSE(length < 42)) GW_DROP("length < 42"); // Support ARP/UDP/ICMP minimums

            uint64_t current_time_ms = tcp_syn_cookie::get_time_counter() * 60000; // Fast amortized temporal baseline

            uint16_t eth_type = core::endian::network_to_host16(*reinterpret_cast<const uint16_t*>(data + 12));
        if (SL_EXPECT_FALSE(eth_type == 0x86DD)) {
            on_raw_frame_ipv6(data, length, conn_id, mbuf);
            mbuf_guard.absorbed = true;
            return;
        }
        if (SL_EXPECT_FALSE(eth_type != 0x0800)) GW_DROP("not ipv4 or ipv6"); // Strictly enforce IPv4/IPv6

            const auto* hdr = reinterpret_cast<const raw_tcp_ipv4_frame*>(data);

            // CRITICAL FIX: IP Options L4 Shift Hijacking & LAND Attack Prevention
            // Enforce rigid 20-byte IPv4 boundaries to prevent struct offset hijacking.
            if (SL_EXPECT_FALSE((hdr->ip_ihl_ver & 0x0F) != 5)) GW_DROP("ip options");
            if (SL_EXPECT_FALSE(hdr->ip_src == hdr->ip_dst)) GW_DROP("land attack");

            // CRITICAL FIX: Topological Poisoning (Broadcast/Multicast Reflection)
            if (SL_EXPECT_FALSE((hdr->ip_src & 0xFF) >= 224)) GW_DROP("mcast src"); // Ignore Multicast/Class E
            if (SL_EXPECT_FALSE(hdr->ip_src == 0xFFFFFFFF)) GW_DROP("bcast src"); // Ignore Global Broadcast

            // CRITICAL FIX: Martian Packet & Loopback Spoofing (CVE-1999-0186)
            if (SL_EXPECT_FALSE((hdr->ip_src & 0xFF) == 127 || (hdr->ip_src & 0xFF) == 0)) GW_DROP("martian ip");

            // CRITICAL FIX: L3 Checksum Evasion (Hardware Agnostic)
            // Checksums MUST be validated before branching into UDP/ICMP logic to prevent 
            // corrupted headers from bypassing protocol verification and poisoning L7.
            uint16_t ip_csum = tcp_wire_engine::compute_checksum(reinterpret_cast<const char*>(hdr) + 14, 20, 0);
            if (SL_EXPECT_FALSE(ip_csum != 0 && ip_csum != 0xFFFF)) GW_DROP("ip csum fail: " << ip_csum);

            // Architecture Invariant: The structural geometry assumes a fixed 64-byte header.
            // Masking 0x3FFF natively rejects More Fragments (MF) and Fragment Offsets 
            // without executing expensive, branch-heavy options parsing.
            uint16_t frag;
            __builtin_memcpy(&frag, &hdr->ip_frag_offset, 2);
            if (SL_EXPECT_FALSE((__builtin_bswap16(frag) & 0x3FFFu) != 0)) GW_DROP("fragmentation unsupported natively here");

            // UDP/TCP Branch Logic
            if (SL_EXPECT_FALSE(hdr->ip_protocol != 6)) {
                if (hdr->ip_protocol == 17 && length >= 42) { // UDP Protocol Handling
                    uint16_t ip_hlen = (hdr->ip_ihl_ver & 0x0F) * 4;
                    if (length >= 14u + ip_hlen + 8u) {
                        // Manual pointer extraction protects against corrupted IP Options offsets
                        const uint16_t* udp_hdr = reinterpret_cast<const uint16_t*>(data + 14 + ip_hlen);
                        uint16_t udp_src_port = udp_hdr[0];
                        uint16_t udp_dst_port = udp_hdr[1];
                        uint16_t udp_length   = udp_hdr[2];
                        uint16_t udp_checksum = udp_hdr[3];
                        
                        uint16_t udp_len = core::endian::network_to_host16(udp_length);
                        
                        // CRITICAL FIX: UDP Security Boundaries (CVE-prevention)
                        // 1. Enforce RFC 768 minimum length (8 bytes) to prevent integer underflow.
                        // 2. Bound UDP length to logical IP length to prevent out-of-bounds reads.
                        // 3. Drop invalid destination port 0 (Source port 0 is allowed per RFC 768).
                        if (SL_EXPECT_FALSE(udp_len < 8)) GW_DROP("udp trunc");
                        if (SL_EXPECT_FALSE(core::endian::network_to_host16(hdr->ip_len) < (ip_hlen + udp_len))) GW_DROP("udp bounds");
                        if (SL_EXPECT_FALSE(udp_dst_port == 0)) GW_DROP("udp zero port");

                        if (length >= 14u + ip_hlen + udp_len) {
                            uint32_t pseudo_sum = 0;
                            uint16_t* psrc = reinterpret_cast<uint16_t*>(const_cast<uint32_t*>(&hdr->ip_src));
                            uint16_t* pdst = reinterpret_cast<uint16_t*>(const_cast<uint32_t*>(&hdr->ip_dst));
                            pseudo_sum += psrc[0]; pseudo_sum += psrc[1];
                            pseudo_sum += pdst[0]; pseudo_sum += pdst[1];
                            pseudo_sum += core::endian::host_to_network16(17);
                            pseudo_sum += udp_length;

                            uint16_t udp_csum = tcp_wire_engine::compute_checksum(udp_hdr, udp_len, pseudo_sum);
                            
                            if (udp_checksum == 0 || udp_csum == 0 || udp_csum == 0xFFFF) {
                                if constexpr (requires { defragmenter_.on_udp(std::declval<inbound_udp_frame>()); }) {
                                    inbound_udp_frame frame{
                                        data + 14 + ip_hlen + 8,
                                        static_cast<std::size_t>(udp_len > 8 ? udp_len - 8 : 0),
                                        hdr->ip_src,
                                        hdr->ip_dst,
                                        udp_src_port,
                                        udp_dst_port
                                    };
                                    defragmenter_.on_udp(frame);
                                }
                            }
                        }
                    }
                }
                return;
            }

            // CRITICAL FIX: Port 0 Multiplexing Filter
            if (SL_EXPECT_FALSE(hdr->tcp_src_port == 0 || hdr->tcp_dst_port == 0)) GW_DROP("tcp zero port");

            if (SL_EXPECT_FALSE(length < 54)) GW_DROP("tcp short"); // Minimum valid TCP segment

            tcp_transmission_control_block& tcb = tcbs_[conn_id & 1023]; // Fast spatial hash

            // CRITICAL FIX: Axiom 16 - Functorial Collision in Spatial Hash Mappings
            // Verify physical 4-tuple against TCB registry to prevent cross-stream payload corruption.
            if (SL_EXPECT_TRUE(tcb.phase_mask != PHASE_LISTEN && tcb.phase_mask != PHASE_CLOSED)) {
                if (SL_EXPECT_FALSE(tcb.remote_ipv4 != hdr->ip_src || 
                                    tcb.local_ipv4 != hdr->ip_dst || 
                                    tcb.remote_port != hdr->tcp_src_port || 
                                    tcb.local_port != hdr->tcp_dst_port)) {
                    GW_DROP("Drop structurally desynchronized manifold: Expected IP " << tcb.remote_ipv4 << " got " << hdr->ip_src);
                }
            } else if (SL_EXPECT_TRUE(tcb.phase_mask == PHASE_LISTEN)) {
                // Axiom 18: Topological Intersection Failure in Ephemeral Multiplexing
                if (SL_EXPECT_FALSE(tcb.local_port != hdr->tcp_dst_port)) GW_DROP("listen port mismatch");
            } else {
                GW_DROP("PHASE_CLOSED excludes unmapped manifolds"); // PHASE_CLOSED strictly excludes unmapped SYN manifolds
            }

            // RFC Option Parsing & Payload Bounding
            uint8_t data_offset_words = hdr->tcp_data_offset >> 4;
            if (SL_EXPECT_FALSE(data_offset_words < 5)) GW_DROP("tcp offset < 5"); // RFC 793: Minimum TCP header is 20 bytes
            uint32_t header_size = 14 + 20 + (data_offset_words * 4);
            if (SL_EXPECT_FALSE(length < header_size)) GW_DROP("tcp offset bounds"); // Truncated packet
            
            // CRITICAL FIX: TCP Flag Anomaly Scans (XMAS, NULL, SYN-FIN)
            // Drop illegal flag combinations commonly used for OS fingerprinting and firewall bypass.
            if (SL_EXPECT_FALSE((hdr->tcp_flags & (FLAG_SYN | FLAG_FIN)) == (FLAG_SYN | FLAG_FIN))) GW_DROP("tcp syn-fin");
            if (SL_EXPECT_FALSE(!(hdr->tcp_flags & (FLAG_SYN | FLAG_ACK | FLAG_RST)))) GW_DROP("tcp bad flags");

            // CRITICAL FIX: Ethernet Padding Payload Corruption
            // We MUST use the logical IP length to extract L7 payload, ignoring physical L2 padding.
            uint32_t logical_len = 14 + core::endian::network_to_host16(hdr->ip_len);
            if (SL_EXPECT_FALSE(length < logical_len)) GW_DROP("hw trunc"); // Hardware truncation
            if (SL_EXPECT_FALSE(logical_len < header_size)) GW_DROP("ip len underflow"); // CRITICAL FIX: IP Length Underflow (OOB Read)
            uint32_t payload_len = logical_len - header_size;
            uint32_t original_payload_len = payload_len;
            const char* payload_ptr = data + header_size;

            // Zero-Allocation SYN-Flood Protection (SYN Cookies)
            if (SL_EXPECT_FALSE(tcb.phase_mask == PHASE_LISTEN)) {
                // Enforce pure SYN to prevent SYN-ACK Reflection Storms
                if ((hdr->tcp_flags & (FLAG_SYN | FLAG_ACK | FLAG_RST | FLAG_FIN)) == FLAG_SYN) {
                    // CRITICAL FIX: Axiom 27 - Isomorphic Reflection of Broadcast Manifolds
                    if (SL_EXPECT_FALSE(hdr->src_mac[0] & 1)) GW_DROP("syn bcast mac");

                    // CRITICAL FIX: "Chinese SYN Attack" / ChinaZ Botnet Defense
                    // Drop SYN packets containing payloads to block volumetric SYN-Data floods.
                    if (SL_EXPECT_FALSE(payload_len > 0)) GW_DROP("syn payload");

                    uint16_t client_mss = 536; // RFC 793 default
                    uint8_t client_wscale = 0;
                    uint8_t client_sack = 0;
                    bool client_ts = false;
                    uint32_t client_tsval = 0;
                    if (data_offset_words > 5) {
                        const uint8_t* opt_ptr = reinterpret_cast<const uint8_t*>(hdr) + 54;
                        int bytes_left = (data_offset_words - 5) * 4;
                        while (bytes_left > 0) {
                            uint8_t kind = opt_ptr[0];
                            if (kind == 0) break;
                            if (kind == 1) { opt_ptr++; bytes_left--; continue; }
                            if (SL_EXPECT_FALSE(bytes_left < 2)) break;
                            uint8_t len = opt_ptr[1];
                            if (len < 2 || len > bytes_left) break;
                            if (kind == 2 && len == 4) { client_mss = (opt_ptr[2] << 8) | opt_ptr[3]; }
                            else if (kind == 3 && len == 3) { client_wscale = opt_ptr[2] > 14 ? 14 : opt_ptr[2]; }
                            else if (kind == 4 && len == 2) { client_sack = 1; }
                            else if (kind == 8 && len == 10) { 
                                client_ts = true; 
                                client_tsval = core::endian::network_to_host32(*reinterpret_cast<const uint32_t*>(opt_ptr + 2)); 
                            }
                            opt_ptr += len; bytes_left -= len;
                        }
                    }
                    
                    // Fully stateless bounce! Drops directly into the gateway's unified egress conduit.
                    uint32_t cookie = tcp_syn_cookie::generate(hdr->ip_src, hdr->tcp_src_port, hdr->ip_dst, hdr->tcp_dst_port, core::endian::network_to_host32(hdr->tcp_seq), client_mss);
                    if (SL_EXPECT_TRUE(gateway_egress_ && tx_mbuf_pool_)) {
                        auto* slot = gateway_egress_->get_reserved_slot(0);
                        if (slot) {
                            slot->mbuf = rte_pktmbuf_alloc(tx_mbuf_pool_);
                            if (slot->mbuf) {
                                std::memset(slot->get_header(), 0, 78);
                                stateless_syn_ack(*slot->get_header(), *hdr, cookie, current_time_ms, client_wscale, client_sack, client_ts, client_tsval);
                                slot->mbuf->data_len = client_ts ? 78 : 66;
                                slot->mbuf->pkt_len = slot->mbuf->data_len;
                                gateway_egress_->commit_n(1);
                            }
                        }
                    }
                    return; // Successfully handled pure SYN
                } else if (hdr->tcp_flags & FLAG_ACK) {
                    uint8_t rec_wscale = 0;
                    uint8_t rec_sack = 0;
                    uint8_t rec_ts = 0;
                    uint32_t rec_tsval = 0;

                    if (data_offset_words > 5) {
                        const uint8_t* opt_ptr = reinterpret_cast<const uint8_t*>(hdr) + 54;
                        int bytes_left = (data_offset_words - 5) * 4;
                        while (bytes_left > 0) {
                            uint8_t kind = opt_ptr[0];
                            if (kind == 0) break;
                            if (kind == 1) { opt_ptr++; bytes_left--; continue; }
                            if (SL_EXPECT_FALSE(bytes_left < 2)) break;
                            uint8_t len = opt_ptr[1];
                            if (len < 2 || len > bytes_left) break;
                            if (kind == 8 && len == 10) { 
                                uint32_t tsecr = core::endian::network_to_host32(*reinterpret_cast<const uint32_t*>(opt_ptr + 6));
                                rec_wscale = (tsecr >> 1) & 0x0F;
                                rec_sack = tsecr & 0x01;
                                rec_ts = 1;
                                rec_tsval = core::endian::network_to_host32(*reinterpret_cast<const uint32_t*>(opt_ptr + 2));
                            }
                            opt_ptr += len; bytes_left -= len;
                        }
                    }

                    auto status = tcp_syn_cookie::validate(hdr->ip_src, hdr->tcp_src_port, hdr->ip_dst, hdr->tcp_dst_port, core::endian::network_to_host32(hdr->tcp_ack) - 1, core::endian::network_to_host32(hdr->tcp_seq) - 1);
                    if (status.is_valid) {
                        // Valid cookie ACK! Hydrate connection into ESTABLISHED.
                        tcb.phase_mask = PHASE_ESTABLISHED;
                        tcb.remote_mss = status.mss;
                        tcb.snd_wscale = rec_wscale;
                        tcb.sack_permitted = rec_sack;
                        tcb.ts_permitted = rec_ts;
                        tcb.ts_recent = rec_tsval;
                        tcb.ecn_permitted = 1;
                        tcb.snd_una = core::endian::network_to_host32(hdr->tcp_ack);
                        tcb.snd_nxt = core::endian::network_to_host32(hdr->tcp_ack);
                        tcb.rcv_nxt = core::endian::network_to_host32(hdr->tcp_seq);
                        tcb.expected_ttl = hdr->ip_ttl;
                        tcb.local_ipv4 = hdr->ip_dst;
                        tcb.remote_ipv4 = hdr->ip_src;
                        tcb.local_port = hdr->tcp_dst_port;
                        tcb.remote_port = hdr->tcp_src_port;
                        __builtin_memcpy(tcb.target_mac, hdr->src_mac, 6);
                        
                        tcb.tx_mbuf_pool = tx_mbuf_pool_;
                        tcb.tx_egress_conduit = gateway_egress_;
                        tcb.tx_unacked_ring = nullptr;
                        tcb.rcv_wnd = 65535;
                        tcb.snd_wnd = static_cast<uint32_t>(core::endian::network_to_host16(hdr->tcp_window)) << rec_wscale;
                        tcb.cwnd = 1460u * 10;
                        tcb.ssthresh = 65535u;

                        // Hand-off the connection to the application layer!
                        if (SL_EXPECT_TRUE(accept_queue_)) {
                            auto* slot = accept_queue_->get_reserved_slot(0);
                            if (slot) { *slot = conn_id; accept_queue_->commit_n(1); }
                            else {
                                tcb.phase_mask = PHASE_CLOSED; // Prevent stranded ghost connection memory leak
                            }
                        }
                        return; // Successfully hydrated
                    } else {
                        GW_DROP("Invalid SYN cookie ACK"); // Drop blind ACKs/Data without touching pool
                    }
                } else {
                    GW_DROP("Non-SYN/ACK on listen"); // Drop non-SYN/ACK without touching pool
                }
            }

            uint32_t trim_offset = 0;
            if (SL_EXPECT_TRUE(tcp_flow_engine::process_inbound(tcb, *hdr, payload_len, trim_offset, mbuf, header_size, &mbuf_guard.absorbed))) {
                
                // If it contains a payload, inject it straight into the defragmenter!
                if (payload_len > 0) {
                    // Create an agnostic stream frame for the upper layers
                    inbound_stream_frame app_frame {
                        payload_ptr + trim_offset,
                        payload_len,
                        conn_id
                    };
                    
                    // The existing TCP Stream Defragmenter handles the LSN ordering
                    // and seamlessly feeds the HTTP/JSON baremetal parsers!
                    defragmenter_.on(app_frame);
                }
                
                // Reassembly: Extract OOO segments that became contiguous
                if (SL_EXPECT_FALSE(tcb.ooo_matrix)) {
                    auto* ooo = static_cast<tcp_spatial_ooo_matrix*>(tcb.ooo_matrix);
                    uint32_t ext_len;
                    uint16_t ext_offset;
                    uint8_t ext_flags;
                    struct rte_mbuf* ext_mbuf;

                    while ((ext_mbuf = ooo->extract_contiguous_mbuf(tcb.rcv_nxt, ext_len, ext_offset, ext_flags)) != nullptr) {
                        tcb.rcv_nxt += ext_len;
                        mbuf_cleanup_guard ext_guard{ext_mbuf}; // CRITICAL FIX: Axiom 31 - RAII connection form guarantees mathematical closure
                        const char* ext_data = rte_pktmbuf_mtod_offset(ext_mbuf, const char*, ext_offset);
                        inbound_stream_frame ooo_frame { ext_data, ext_len, conn_id };
                        defragmenter_.on(ooo_frame);

                        if (SL_EXPECT_FALSE(ext_flags & FLAG_FIN)) {
                            tcb.rcv_nxt++;
                            tcb.flags_pending |= FLAG_ACK;
                            if (tcb.phase_mask == PHASE_ESTABLISHED || tcb.phase_mask == PHASE_SYN_RCVD) tcb.phase_mask = PHASE_CLOSE_WAIT;
                            else if (tcb.phase_mask == PHASE_FIN_WAIT1) tcb.phase_mask = PHASE_CLOSING;
                            else if (tcb.phase_mask == PHASE_FIN_WAIT2) {
                                tcb.phase_mask = PHASE_TIME_WAIT;
                                tcb.time_wait_deadline_tsc = 0;
                            }
                        }
                    }
                }

                // TX/ACK protocol generation logic
                
                // CRITICAL FIX: Axiom 8 - Allocate OOO matrix for pure FLAG_FIN geometries
                if ((original_payload_len > 0 || (hdr->tcp_flags & FLAG_FIN)) && payload_len == 0 && (tcb.flags_pending & FLAG_ACK) && !tcb.ooo_matrix) {
                    tcb.ooo_matrix = ooo_pool_.make_raw();
                    if (tcb.ooo_matrix) {
                        static_cast<tcp_spatial_ooo_matrix*>(tcb.ooo_matrix)->reset(tcb.rcv_nxt);
                        uint32_t host_seq = core::endian::network_to_host32(hdr->tcp_seq);
                        uint32_t payload_seq = host_seq + ((hdr->tcp_flags & FLAG_SYN) ? 1 : 0);
                        if (payload_seq - tcb.rcv_nxt <= 0x7FFFFFFF && mbuf) {
                            // CRITICAL FIX: L2 Padding Protocol Smuggling (OOO Matrix Extractor)
                            mbuf_guard.absorbed = static_cast<tcp_spatial_ooo_matrix*>(tcb.ooo_matrix)->insert_mbuf(tcb.rcv_nxt, payload_seq, mbuf, header_size, original_payload_len, hdr->tcp_flags);
                        }
                    }
                }

                tcp_flow_engine::dispatch_control_frames(tcb);
            }
        }

        /**
         * @brief Background temporal sweep for retransmissions, timeouts, and teardowns.
         */
        SLAB_HOT void poll_temporal(uint64_t current_time_ms) noexcept {
            for (uint32_t i = 0; i < 1024; ++i) {
                tcp_transmission_control_block& tcb = tcbs_[i];
                if (SL_EXPECT_FALSE(tcb.phase_mask == PHASE_CLOSED || tcb.phase_mask == PHASE_LISTEN)) {
                    // VULNERABILITY FIX: Clean up DPDK MBUFs on Internal Gateway Teardowns
                    if (SL_EXPECT_FALSE(tcb.ooo_matrix)) {
                        static_cast<tcp_spatial_ooo_matrix*>(tcb.ooo_matrix)->reset(0);
                    }
                    
                    // CRITICAL FIX: Axiom 1 - Topological Orphan Isolation in MBUF Substrate Rings
                    if (SL_EXPECT_FALSE(tcb.tx_unacked_ring)) {
                        auto* unacked = static_cast<core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024>*>(tcb.tx_unacked_ring);
                        while (unacked->available_to_peek() > 0) {
                            const_cast<outbound_tcp_segment<1460>*>(unacked->get_peek_slot(0))->release();
                            unacked->consume_n(1);
                        }
                    }
                    continue;
                }

                uint32_t prior_snd_nxt = tcb.snd_nxt;

                // Temporal Wheel handles RTO, Delayed ACKs, ZWPs, TIME_WAIT, Keep-Alive
                tcp_temporal_wheel::on_tick(current_time_ms, tcb, reinterpret_cast<rto_tracker&>(tcb.srtt));

                // Active Retransmission Replay
                // CRITICAL FIX: Axiom 2 - Lexicographical Disjunction in Trans-Boundary Rollbacks
                if (SL_EXPECT_FALSE(static_cast<int32_t>(tcb.snd_nxt - prior_snd_nxt) < 0)) {
                    // Axiom 21: Gauge Field Dislocation in Saturated Zero-Window Projections
                    if (SL_EXPECT_FALSE(tcb.snd_wnd == 0)) {
                        tcb.temporal_flags |= TEMP_FLAG_ZWP_ACTIVE;
                        tcb.snd_nxt = prior_snd_nxt; // Cancel rewind, yield to ZWP
                    } else {
                        // RTO expired, snd_nxt rewound. Replay from unacked_ring
                        if (tcb.tx_unacked_ring && tcb.tx_egress_conduit) {
                        auto* unacked = static_cast<core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024>*>(tcb.tx_unacked_ring);
                        auto* egress = static_cast<core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024>*>(tcb.tx_egress_conduit);
                        
                        size_t available = unacked->available_to_peek();
                        uint32_t usable_window = std::min(tcb.cwnd, tcb.snd_wnd);
                        
                        for (size_t j = 0; j < available; ++j) {
                            const outbound_tcp_segment<1460>* lost_frame = unacked->get_peek_slot(j);
                            uint32_t frame_len = lost_frame->get_payload_length() + ((lost_frame->get_header()->tcp_flags & (FLAG_SYN | FLAG_FIN)) ? 1 : 0);
                            if (tcb.snd_nxt - tcb.snd_una + frame_len > usable_window) break; // Strict Congestion Control Guard
                            
                            outbound_tcp_segment<1460>* slot = egress->get_reserved_slot(0);
                            if (slot) {
                                slot->mbuf = lost_frame->mbuf;
                                if (SL_EXPECT_FALSE(!slot->retain())) {
                                    tcb.phase_mask = PHASE_CLOSED;
                                    tcb.flags_pending |= FLAG_RST;
                                    break;
                                }

                                slot->get_header()->tcp_ack = core::endian::host_to_network32(tcb.rcv_nxt);       // Update ACK dynamically
                                uint32_t wnd = tcb.rcv_wnd >> tcb.rcv_wscale;
                                slot->get_header()->tcp_window = core::endian::host_to_network16(wnd > 65535 ? 65535 : wnd);    // Update Window dynamically

                                if (tcb.is_ipv6) {
                                    auto* rtx_hdr = reinterpret_cast<raw_tcp_ipv6_frame*>(slot->get_header());
                                    uint16_t tcp_len = core::endian::network_to_host16(rtx_hdr->ipv6_plen);
                                    uint32_t pseudo_sum = 0;
                                    const uint16_t* src16 = reinterpret_cast<const uint16_t*>(rtx_hdr->ipv6_src);
                                    const uint16_t* dst16 = reinterpret_cast<const uint16_t*>(rtx_hdr->ipv6_dst);
                                    for(int i=0; i<8; ++i) pseudo_sum += src16[i];
                                    for(int i=0; i<8; ++i) pseudo_sum += dst16[i];
                                    pseudo_sum += core::endian::host_to_network16(6);
                                    pseudo_sum += core::endian::host_to_network16(tcp_len);
                                    rtx_hdr->tcp_checksum = 0;
                                    rtx_hdr->tcp_checksum = tcp_wire_engine::compute_checksum(&rtx_hdr->tcp_src_port, tcp_len, pseudo_sum);
                                } else {
                                    auto* rtx_hdr = slot->get_header();
                                    rtx_hdr->ip_checksum = 0;
                                    rtx_hdr->ip_checksum = tcp_wire_engine::compute_checksum(&rtx_hdr->ip_ihl_ver, 20, 0);
                                    uint16_t tcp_len = core::endian::network_to_host16(rtx_hdr->ip_len) - ((rtx_hdr->ip_ihl_ver & 0x0F) * 4);
                                    uint32_t pseudo_sum = 0;
                                    uint16_t* psrc = reinterpret_cast<uint16_t*>(&rtx_hdr->ip_src);
                                    uint16_t* pdst = reinterpret_cast<uint16_t*>(&rtx_hdr->ip_dst);
                                    pseudo_sum += psrc[0]; pseudo_sum += psrc[1];
                                    pseudo_sum += pdst[0]; pseudo_sum += pdst[1];
                                    pseudo_sum += core::endian::host_to_network16(6);
                                    pseudo_sum += core::endian::host_to_network16(tcp_len);
                                    rtx_hdr->tcp_checksum = 0;
                                    rtx_hdr->tcp_checksum = tcp_wire_engine::compute_checksum(&rtx_hdr->tcp_src_port, tcp_len, pseudo_sum);
                                }

                                egress->commit_n(1);
                                tcb.snd_nxt += frame_len; // FIX: Manually advance snd_nxt as prepare_outbound_header is bypassed
                            } else {
                                break; // Egress full, hardware backpressure triggers natural delay
                            }
                        }
                        }
                    }
                }

                // Flush pending ACKs or Window Probes requested by the temporal wheel
                tcp_flow_engine::dispatch_control_frames(tcb);
            }
        }

        SLAB_HOT void on_raw_frame_ipv6(const char* data, size_t length, size_t conn_id, struct rte_mbuf* mbuf) noexcept {
            mbuf_cleanup_guard mbuf_guard{mbuf};
            if (SL_EXPECT_FALSE(length < 74)) GW_DROP("ipv6 short");

            uint64_t current_time_ms = tcp_syn_cookie::get_time_counter() * 60000;

            const auto* hdr = reinterpret_cast<const raw_tcp_ipv6_frame*>(data);

            if (SL_EXPECT_FALSE((reinterpret_cast<const uint8_t*>(&hdr->ipv6_src[0]))[0] == 0xFF)) GW_DROP("ipv6 mcast src");

            uint16_t ipv6_plen = core::endian::network_to_host16(hdr->ipv6_plen);
            if (SL_EXPECT_FALSE(length < 54u + ipv6_plen)) GW_DROP("ipv6 hw trunc");
            uint32_t logical_len = 14 + 40 + ipv6_plen;
            
            uint8_t nxt_hdr = hdr->ipv6_nxt;
            uint32_t l4_offset = 54;

            if (nxt_hdr == 0) { // Hop-by-Hop Options
                if (SL_EXPECT_FALSE(logical_len < l4_offset + 8)) GW_DROP("ipv6 hbh trunc");
                nxt_hdr = data[l4_offset];
                uint32_t ext_len = (data[l4_offset + 1] + 1) * 8;
                if (SL_EXPECT_FALSE(logical_len < l4_offset + ext_len)) GW_DROP("ipv6 hbh bounds");
                l4_offset += ext_len;
            }

            // CRITICAL FIX: IPv6 Extension Header Chaining (RFC 8200)
            // Iteratively process extension headers, advancing l4_offset.
            // Any unrecognized or malformed extension header chain will be dropped.
            while (nxt_hdr != 6 && nxt_hdr != 17 && nxt_hdr != 58 && nxt_hdr != 59) { // TCP, UDP, ICMPv6, No Next Header
                if (l4_offset + 8 > logical_len) GW_DROP("ipv6 ext hdr trunc"); // Min extension header size
                
                // Next Header byte is at offset 0 of the current extension header
                uint8_t current_ext_hdr_type = data[l4_offset];
                uint32_t current_ext_hdr_len = (data[l4_offset + 1] + 1) * 8; // Length in 8-octet units
                
                if (l4_offset + current_ext_hdr_len > logical_len) GW_DROP("ipv6 ext hdr bounds");
                nxt_hdr = current_ext_hdr_type;
                l4_offset += current_ext_hdr_len;
            }

            if (nxt_hdr == 17) {
                if (logical_len >= l4_offset + 8) {
                    const uint16_t* udp_hdr = reinterpret_cast<const uint16_t*>(data + l4_offset);
                    uint16_t udp_src_port = udp_hdr[0];
                    uint16_t udp_dst_port = udp_hdr[1];
                    uint16_t udp_length   = udp_hdr[2];
                    uint16_t udp_checksum = udp_hdr[3];
                    
                    uint16_t udp_len = core::endian::network_to_host16(udp_length);
                    if (SL_EXPECT_FALSE(udp_len < 8)) GW_DROP("udp6 trunc");
                    if (SL_EXPECT_FALSE(logical_len < l4_offset + udp_len)) GW_DROP("udp6 bounds");
                    if (SL_EXPECT_FALSE(udp_dst_port == 0)) GW_DROP("udp6 zero port");
                    
                    uint32_t pseudo_sum = 0;
                    const uint16_t* src16 = reinterpret_cast<const uint16_t*>(hdr->ipv6_src);
                    const uint16_t* dst16 = reinterpret_cast<const uint16_t*>(hdr->ipv6_dst);
                    for(int i=0; i<8; ++i) pseudo_sum += src16[i];
                    for(int i=0; i<8; ++i) pseudo_sum += dst16[i];
                    pseudo_sum += core::endian::host_to_network16(17);
                    pseudo_sum += udp_length;
                    
                    uint16_t udp_csum = tcp_wire_engine::compute_checksum(udp_hdr, udp_len, pseudo_sum);
                    
                    if (udp_checksum == 0 || udp_csum == 0 || udp_csum == 0xFFFF) {
                        if constexpr (requires { defragmenter_.on_udp_ipv6(std::declval<inbound_udp_ipv6_frame>()); }) {
                            inbound_udp_ipv6_frame frame{
                                data + l4_offset + 8,
                                static_cast<std::size_t>(udp_len > 8 ? udp_len - 8 : 0),
                                {hdr->ipv6_src[0], hdr->ipv6_src[1]},
                                {hdr->ipv6_dst[0], hdr->ipv6_dst[1]},
                                udp_src_port,
                                udp_dst_port
                            };
                            defragmenter_.on_udp_ipv6(frame);
                        }
                    }
                }
                return;
            }

            // Check for No Next Header (59) which means packet is dropped
            if (SL_EXPECT_FALSE(nxt_hdr == 59)) GW_DROP("ipv6 no next hdr");

            // Re-evaluate current L4 offset after processing all extension headers.
            // If still not TCP, drop.
            if (SL_EXPECT_FALSE(nxt_hdr != 6)) GW_DROP("ipv6 unsupported L4 after ext hdrs");

            // If TCP, ensure the L4 offset is sane after all extension headers.
            // This acts as a final guard against malformed or excessively long chains
            // that push the TCP header beyond expected boundaries.
            if (SL_EXPECT_FALSE(l4_offset > (14 + 40 + 64))) GW_DROP("ipv6 tcp hdr too far"); // Max reasonable L4 offset after extensions (IPv6 Hdr + some extensions)

            if (SL_EXPECT_FALSE(hdr->tcp_src_port == 0 || hdr->tcp_dst_port == 0)) GW_DROP("tcp zero port");

            tcp_transmission_control_block& tcb = tcbs_[conn_id & 1023];

            if (SL_EXPECT_TRUE(tcb.phase_mask != PHASE_LISTEN && tcb.phase_mask != PHASE_CLOSED)) {
                if (SL_EXPECT_FALSE(!tcb.is_ipv6 ||
                                    tcb.remote_ipv6[0] != hdr->ipv6_src[0] || tcb.remote_ipv6[1] != hdr->ipv6_src[1] ||
                                    tcb.local_ipv6[0] != hdr->ipv6_dst[0] || tcb.local_ipv6[1] != hdr->ipv6_dst[1] ||
                                    tcb.remote_port != hdr->tcp_src_port || 
                                    tcb.local_port != hdr->tcp_dst_port)) {
                    GW_DROP("Drop structurally desynchronized manifold");
                }
            } else if (SL_EXPECT_TRUE(tcb.phase_mask == PHASE_LISTEN)) {
                if (SL_EXPECT_FALSE(tcb.local_port != hdr->tcp_dst_port)) GW_DROP("listen port mismatch");
            } else {
                GW_DROP("PHASE_CLOSED excludes unmapped manifolds");
            }

            uint8_t data_offset_words = hdr->tcp_data_offset >> 4;
            if (SL_EXPECT_FALSE(data_offset_words < 5)) GW_DROP("tcp offset < 5");
            uint32_t header_size = 14 + 40 + (data_offset_words * 4);
            if (SL_EXPECT_FALSE(logical_len < header_size)) GW_DROP("tcp offset bounds");
            
            if (SL_EXPECT_FALSE((hdr->tcp_flags & (FLAG_SYN | FLAG_FIN)) == (FLAG_SYN | FLAG_FIN))) GW_DROP("tcp syn-fin");
            if (SL_EXPECT_FALSE(!(hdr->tcp_flags & (FLAG_SYN | FLAG_ACK | FLAG_RST)))) GW_DROP("tcp bad flags");

            uint32_t payload_len = logical_len - header_size;
            uint32_t original_payload_len = payload_len;
            const char* payload_ptr = data + header_size;

            if (SL_EXPECT_FALSE(tcb.phase_mask == PHASE_LISTEN)) {
                if ((hdr->tcp_flags & (FLAG_SYN | FLAG_ACK | FLAG_RST | FLAG_FIN)) == FLAG_SYN) {
                    if (SL_EXPECT_FALSE(hdr->src_mac[0] & 1)) GW_DROP("syn bcast mac");
                    if (SL_EXPECT_FALSE(payload_len > 0)) GW_DROP("syn payload");

                    uint16_t client_mss = 1220; // IPv6 minimum
                    uint8_t client_wscale = 0;
                    uint8_t client_sack = 0;
                    bool client_ts = false;
                    uint32_t client_tsval = 0;
                    if (data_offset_words > 5) {
                        const uint8_t* opt_ptr = reinterpret_cast<const uint8_t*>(hdr) + 74;
                        int bytes_left = (data_offset_words - 5) * 4;
                        while (bytes_left > 0) {
                            uint8_t kind = opt_ptr[0];
                            if (kind == 0) break;
                            if (kind == 1) { opt_ptr++; bytes_left--; continue; }
                            if (SL_EXPECT_FALSE(bytes_left < 2)) break;
                            uint8_t len = opt_ptr[1];
                            if (len < 2 || len > bytes_left) break;
                            if (kind == 2 && len == 4) { client_mss = (opt_ptr[2] << 8) | opt_ptr[3]; }
                            else if (kind == 3 && len == 3) { client_wscale = opt_ptr[2] > 14 ? 14 : opt_ptr[2]; }
                            else if (kind == 4 && len == 2) { client_sack = 1; }
                            else if (kind == 8 && len == 10) { 
                                client_ts = true; 
                                client_tsval = core::endian::network_to_host32(*reinterpret_cast<const uint32_t*>(opt_ptr + 2)); 
                            }
                            opt_ptr += len; bytes_left -= len;
                        }
                    }
                    
                    uint32_t cookie = tcp_syn_cookie::generate_ipv6(hdr->ipv6_src, hdr->tcp_src_port, hdr->ipv6_dst, hdr->tcp_dst_port, core::endian::network_to_host32(hdr->tcp_seq), client_mss, current_time_ms);
                    if (SL_EXPECT_TRUE(gateway_egress_ && tx_mbuf_pool_)) {
                        auto* slot = gateway_egress_->get_reserved_slot(0);
                        if (slot) {
                            slot->mbuf = rte_pktmbuf_alloc(tx_mbuf_pool_);
                            if (slot->mbuf) {
                                std::memset(slot->get_header(), 0, 128);
                                stateless_syn_ack_ipv6(*reinterpret_cast<raw_tcp_ipv6_frame*>(slot->get_header()), *hdr, cookie, current_time_ms, client_wscale, client_sack, client_ts, client_tsval);
                                slot->mbuf->data_len = client_ts ? 98 : 86;
                                slot->mbuf->pkt_len = slot->mbuf->data_len;
                                gateway_egress_->commit_n(1);
                            }
                        }
                    }
                    return;
                } else if (hdr->tcp_flags & FLAG_ACK) {
                    uint8_t rec_wscale = 0;
                    uint8_t rec_sack = 0;
                    uint8_t rec_ts = 0;
                    uint32_t rec_tsval = 0;

                    if (data_offset_words > 5) {
                        const uint8_t* opt_ptr = reinterpret_cast<const uint8_t*>(hdr) + 74;
                        int bytes_left = (data_offset_words - 5) * 4;
                        while (bytes_left > 0) {
                            uint8_t kind = opt_ptr[0];
                            if (kind == 0) break;
                            if (kind == 1) { opt_ptr++; bytes_left--; continue; }
                            if (SL_EXPECT_FALSE(bytes_left < 2)) break;
                            uint8_t len = opt_ptr[1];
                            if (len < 2 || len > bytes_left) break;
                            if (kind == 8 && len == 10) { 
                                uint32_t tsecr = core::endian::network_to_host32(*reinterpret_cast<const uint32_t*>(opt_ptr + 6));
                                rec_wscale = (tsecr >> 1) & 0x0F;
                                rec_sack = tsecr & 0x01;
                                rec_ts = 1;
                                rec_tsval = core::endian::network_to_host32(*reinterpret_cast<const uint32_t*>(opt_ptr + 2));
                            }
                            opt_ptr += len; bytes_left -= len;
                        }
                    }

                    auto status = tcp_syn_cookie::validate_ipv6(hdr->ipv6_src, hdr->tcp_src_port, hdr->ipv6_dst, hdr->tcp_dst_port, core::endian::network_to_host32(hdr->tcp_ack) - 1, core::endian::network_to_host32(hdr->tcp_seq) - 1, current_time_ms);
                    if (status.is_valid) {
                        tcb.phase_mask = PHASE_ESTABLISHED;
                        tcb.remote_mss = status.mss;
                        tcb.snd_wscale = rec_wscale;
                        tcb.sack_permitted = rec_sack;
                        tcb.ts_permitted = rec_ts;
                        tcb.ts_recent = rec_tsval;
                        tcb.ecn_permitted = 1;
                        tcb.snd_una = core::endian::network_to_host32(hdr->tcp_ack);
                        tcb.snd_nxt = core::endian::network_to_host32(hdr->tcp_ack);
                        tcb.rcv_nxt = core::endian::network_to_host32(hdr->tcp_seq);
                        tcb.expected_ttl = hdr->ipv6_hlim;
                        tcb.local_ipv6[0] = hdr->ipv6_dst[0];
                        tcb.local_ipv6[1] = hdr->ipv6_dst[1];
                        tcb.remote_ipv6[0] = hdr->ipv6_src[0];
                        tcb.remote_ipv6[1] = hdr->ipv6_src[1];
                        tcb.local_port = hdr->tcp_dst_port;
                        tcb.remote_port = hdr->tcp_src_port;
                        tcb.is_ipv6 = 1;
                        __builtin_memcpy(tcb.target_mac, hdr->src_mac, 6);
                        
                        tcb.tx_mbuf_pool = tx_mbuf_pool_;
                        tcb.tx_egress_conduit = gateway_egress_;
                        tcb.tx_unacked_ring = nullptr;
                        tcb.rcv_wnd = 65535;
                        tcb.snd_wnd = static_cast<uint32_t>(core::endian::network_to_host16(hdr->tcp_window)) << rec_wscale;
                        tcb.cwnd = 1460u * 10;
                        tcb.ssthresh = 65535u;

                        if (SL_EXPECT_TRUE(accept_queue_)) {
                            auto* slot = accept_queue_->get_reserved_slot(0);
                            if (slot) { *slot = conn_id; accept_queue_->commit_n(1); }
                            else { tcb.phase_mask = PHASE_CLOSED; }
                        }
                        return;
                    } else {
                        GW_DROP("Invalid SYN cookie ACK");
                    }
                } else {
                    GW_DROP("Non-SYN/ACK on listen");
                }
            }

            uint32_t trim_offset = 0;
            if (SL_EXPECT_TRUE(tcp_flow_engine::process_inbound(tcb, *hdr, payload_len, trim_offset, mbuf, header_size, &mbuf_guard.absorbed))) {
                
                if (payload_len > 0) {
                    inbound_stream_frame app_frame {
                        payload_ptr + trim_offset,
                        payload_len,
                        conn_id
                    };
                    defragmenter_.on(app_frame);
                }
                
                if (SL_EXPECT_FALSE(tcb.ooo_matrix)) {
                    auto* ooo = static_cast<tcp_spatial_ooo_matrix*>(tcb.ooo_matrix);
                    uint32_t ext_len;
                    uint16_t ext_offset;
                    uint8_t ext_flags;
                    struct rte_mbuf* ext_mbuf;

                    while ((ext_mbuf = ooo->extract_contiguous_mbuf(tcb.rcv_nxt, ext_len, ext_offset, ext_flags)) != nullptr) {
                        tcb.rcv_nxt += ext_len;
                        mbuf_cleanup_guard ext_guard{ext_mbuf};
                        const char* ext_data = rte_pktmbuf_mtod_offset(ext_mbuf, const char*, ext_offset);
                        inbound_stream_frame ooo_frame { ext_data, ext_len, conn_id };
                        defragmenter_.on(ooo_frame);

                        if (SL_EXPECT_FALSE(ext_flags & FLAG_FIN)) {
                            tcb.rcv_nxt++;
                            tcb.flags_pending |= FLAG_ACK;
                            if (tcb.phase_mask == PHASE_ESTABLISHED || tcb.phase_mask == PHASE_SYN_RCVD) tcb.phase_mask = PHASE_CLOSE_WAIT;
                            else if (tcb.phase_mask == PHASE_FIN_WAIT1) tcb.phase_mask = PHASE_CLOSING;
                            else if (tcb.phase_mask == PHASE_FIN_WAIT2) {
                                tcb.phase_mask = PHASE_TIME_WAIT;
                                tcb.time_wait_deadline_tsc = 0;
                            }
                        }
                    }
                }

                if ((original_payload_len > 0 || (hdr->tcp_flags & FLAG_FIN)) && payload_len == 0 && (tcb.flags_pending & FLAG_ACK) && !tcb.ooo_matrix) {
                    tcb.ooo_matrix = ooo_pool_.make_raw();
                    if (tcb.ooo_matrix) {
                        static_cast<tcp_spatial_ooo_matrix*>(tcb.ooo_matrix)->reset(tcb.rcv_nxt);
                        uint32_t host_seq = core::endian::network_to_host32(hdr->tcp_seq);
                        uint32_t payload_seq = host_seq + ((hdr->tcp_flags & FLAG_SYN) ? 1 : 0);
                        if (payload_seq - tcb.rcv_nxt <= 0x7FFFFFFF && mbuf) {
                            mbuf_guard.absorbed = static_cast<tcp_spatial_ooo_matrix*>(tcb.ooo_matrix)->insert_mbuf(tcb.rcv_nxt, payload_seq, mbuf, header_size, original_payload_len, hdr->tcp_flags);
                        }
                    }
                }

                tcp_flow_engine::dispatch_control_frames(tcb);
            }
        }
    };
} // namespace slabflux::net