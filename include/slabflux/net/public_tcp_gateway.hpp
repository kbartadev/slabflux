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
 * ============================================================================* @file public_tcp_gateway.hpp
 * @brief Public-facing TCP Edge Gateway supporting C1M concurrency.
 */

#pragma once

#include "slabflux/net/tcp_flow_engine.hpp"
#include "slabflux/net/virtual_tcp_socket.hpp"
#include "slabflux/net/raw_tcp_ipv4_frame.hpp"
#include "slabflux/net/raw_tcp_ipv6_frame.hpp"
#include "slabflux/net/raw_udp_ipv4_frame.hpp"
#include "slabflux/net/raw_udp_ipv6_frame.hpp"
#include "slabflux/core/endian.hpp"
#include "slabflux/net/ip_spatial_defragmenter.hpp"
#include "slabflux/net/tcp_spatial_ooo_matrix.hpp"
#include "slabflux/net/tcp_temporal_wheel.hpp"
#include "slabflux/net/tcp_syn_cookie.hpp"
#include "slabflux/net/raw_arp_frame.hpp"
#include "slabflux/net/arp_matrix.hpp"
#include "slabflux/net/ndp_matrix.hpp"
#include "slabflux/core/mpsc_pool.hpp"
#include <atomic>
#include <rte_ip.h>
#include <iostream>
#ifndef SLAB_GW_DEBUG
#define SLAB_GW_DEBUG(msg) std::cerr << "[GW DROP " << __LINE__ << "] " << msg << std::endl
#define GW_DROP(msg) do { SLAB_GW_DEBUG(msg); return; } while(0)
#define GW_DROP_FALSE(msg) do { SLAB_GW_DEBUG(msg); return false; } while(0)
#endif
#include <rte_ip.h>
#include <stdexcept>
#include <x86intrin.h>
#include "slabflux/core/epoch_manager.hpp"
#include "slabflux/core/ebr_reclamation_queue.hpp"
#include "slabflux/net/tcp_wire_engine.hpp"
#include "slabflux/net/ip_spatial_defragmenter_ipv6.hpp" // New IPv6 defrag

namespace slabflux::net {

    // 1. Precise 4-Tuple Connection Identity
    struct tcp_4tuple {
        union {
            struct {
                uint32_t src_ip;
                uint32_t dst_ip;
            };
            struct {
                uint64_t src_ipv6[2];
                uint64_t dst_ipv6[2];
            };
        };
        uint16_t src_port;
        uint16_t dst_port;
        uint8_t  is_ipv6;

        bool operator==(const tcp_4tuple& o) const noexcept {
            if (is_ipv6 != o.is_ipv6 || src_port != o.src_port || dst_port != o.dst_port) return false;
            if (is_ipv6) {
                return src_ipv6[0] == o.src_ipv6[0] && src_ipv6[1] == o.src_ipv6[1] &&
                       dst_ipv6[0] == o.dst_ipv6[0] && dst_ipv6[1] == o.dst_ipv6[1];
            }
            return src_ip == o.src_ip && dst_ip == o.dst_ip;
        }
    };

    // 2. Cryptographically Secure Hash to prevent Dictionary DoS Attacks
    struct tcp_4tuple_hash {
        static inline uint32_t hash(const tcp_4tuple& k) noexcept {
            if (k.is_ipv6) {
                __m128i b1 = _mm_set_epi64x(k.src_ipv6[0], k.src_ipv6[1]);
                __m128i b2 = _mm_set_epi64x(k.dst_ipv6[0], k.dst_ipv6[1]);
                b1 = _mm_aesenc_si128(b1, tcp_syn_cookie::rk[0]);
                b2 = _mm_aesenc_si128(b2, tcp_syn_cookie::rk[1]);
                b1 = _mm_xor_si128(b1, b2);
                return static_cast<uint32_t>(_mm_extract_epi32(b1, 0)) ^ ((k.src_port << 16) | k.dst_port);
            } else {
                __m128i block = _mm_set_epi32(k.src_ip, k.dst_ip, (k.src_port << 16) | k.dst_port, 0);
                block = _mm_aesenc_si128(block, tcp_syn_cookie::rk[0]);
                block = _mm_aesenc_si128(block, tcp_syn_cookie::rk[1]);
                return static_cast<uint32_t>(_mm_extract_epi32(block, 0));
            }
        }
    };

    struct inbound_udp_ipv6_frame {
        const char* data;
        std::size_t payload_length;
        uint64_t src_ipv6[2];
        uint64_t dst_ipv6[2];
        uint16_t src_port;
        uint16_t dst_port;
    };

    template<typename Defragmenter, size_t MaxConnections = 1048576>
    class alignas(core::CACHE_LINE_SIZE) public_tcp_gateway {
        Defragmenter& defragmenter_;
        
        // Dynamic zero-allocation pools for massive scalability
        core::mpsc_pool<tcp_transmission_control_block, MaxConnections> tcb_pool_;
        
        // Slowloris Guard: We only allocate OOO matrices for connections actively experiencing loss
        core::mpsc_pool<tcp_spatial_ooo_matrix, MaxConnections / 4> ooo_pool_; 

        // Lock-free Hash Map components
        struct hash_node {
            tcp_4tuple key;
            tcp_transmission_control_block* tcb;
            uint32_t conn_id;
            std::atomic<hash_node*> next;
        };

        core::mpsc_pool<hash_node, MaxConnections> node_pool_;
        std::atomic<hash_node*>* buckets_;
        static constexpr size_t NUM_BUCKETS = MaxConnections;
        std::atomic<uint32_t> next_conn_id_{1};
        size_t sweep_cursor_{0}; // Amortized temporal sweeper state
        
        core::ebr_reclamation_queue<hash_node, MaxConnections> ebr_graveyard_;
        core::epoch_manager<1> epoch_mgr_; // Gateway is single-threaded per RSS core

        struct node_pool_wrapper {
            core::mpsc_pool<tcp_transmission_control_block, MaxConnections>& tcb_pool;
            core::mpsc_pool<hash_node, MaxConnections>& node_pool;
            SLAB_FORCE_INLINE void release(hash_node* node) noexcept {
                tcb_pool.release(node->tcb);
                node_pool.release(node);
            }
        } pool_wrapper_{tcb_pool_, node_pool_};

        ip_spatial_defragmenter<1024> ip_defrag_;
        ip_spatial_defragmenter_ipv6<1024> ip_defrag_ipv6_; // IPv6 defragmenter

        struct rte_mempool* tx_mbuf_pool_{nullptr};
        arp_matrix<4096> arp_cache_;
        ndp_matrix<4096> ndp_cache_;
        uint32_t local_ipv4_{0};
        uint8_t local_mac_[6]{0};
        uint32_t subnet_mask_{0xFFFFFFFF};
        uint32_t default_gateway_ipv4_{0};
        std::atomic<uint32_t> ephemeral_port_cursor_{0};
        uint64_t last_icmp_reset_ms_{0};
        uint32_t icmp_tokens_{100};
        uint64_t last_rst_reset_ms_{0};
        uint32_t rst_tokens_{100};
        
        uint64_t local_ipv6_[2]{0, 0}; // IPv6 Local Identity

        struct mbuf_cleanup_guard {
            struct rte_mbuf*& ptr;
            bool absorbed{false};
            ~mbuf_cleanup_guard() {
                if (ptr && !absorbed) rte_pktmbuf_free(ptr);
            }
        };

        core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024>* gateway_egress_{nullptr};
        core::spsc_ring_conduit<uint32_t, 1024>* accept_queue_{nullptr};

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

        explicit public_tcp_gateway(Defragmenter& defrag) noexcept : defragmenter_(defrag) {
            // Standard heap allocation for the static spine of the hash map
            buckets_ = new std::atomic<hash_node*>[NUM_BUCKETS];
            for(size_t i = 0; i < NUM_BUCKETS; ++i) {
                buckets_[i].store(nullptr, std::memory_order_relaxed);
            }
        }

        ~public_tcp_gateway() {
            delete[] buckets_;
        }

        void bind_conduits(core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024>* egress,
                           core::spsc_ring_conduit<uint32_t, 1024>* accept) noexcept {
            gateway_egress_ = egress;
            accept_queue_ = accept;
        }

        void bind_mempool(struct rte_mempool* pool) noexcept {
            tx_mbuf_pool_ = pool;
        }

        void set_local_identity(uint32_t ipv4, const uint8_t* mac, uint32_t subnet_mask = 0xFFFFFFFF, uint32_t default_gateway = 0) noexcept {
            local_ipv4_ = ipv4;
            std::memcpy(local_mac_, mac, 6);
            subnet_mask_ = subnet_mask;
            default_gateway_ipv4_ = default_gateway;
        }

        void set_local_identity_v6(const uint64_t ipv6[2]) noexcept {
            local_ipv6_[0] = ipv6[0];
            local_ipv6_[1] = ipv6[1];
        }

    /**
     * @brief Natively injects a stateless UDP datagram onto the egress wire.
     * @return True if enqueued, False if dropped (e.g. queue full or awaiting ARP resolution).
     */
    SLAB_HOT bool send_udp(uint32_t target_ip, uint16_t target_port, uint16_t source_port, const char* payload, size_t payload_len) noexcept {
        // CRITICAL FIX: Prevent DPDK MBUF memory corruption & Ethernet Giant Frame drops
        // Standard Ethernet MTU (1500) - IPv4 (20) - UDP (8) = 1472 bytes max payload.
        if (SL_EXPECT_FALSE(payload_len > 1472)) GW_DROP_FALSE("udp payload > 1472");

        // CRITICAL FIX: L7 Null Pointer Segfault (Kernel copy_from_user trap equivalent)
        if (SL_EXPECT_FALSE(payload == nullptr && payload_len > 0)) GW_DROP_FALSE("udp payload null");

        if (SL_EXPECT_FALSE(!gateway_egress_ || !tx_mbuf_pool_)) GW_DROP_FALSE("no egress or pool");

        auto* slot = gateway_egress_->get_reserved_slot(0);
        if (SL_EXPECT_FALSE(!slot)) GW_DROP_FALSE("no egress slot");

        slot->mbuf = rte_pktmbuf_alloc(tx_mbuf_pool_);
        if (SL_EXPECT_FALSE(!slot->mbuf)) GW_DROP_FALSE("mbuf alloc fail");

        uint32_t next_hop_ip = (target_ip & subnet_mask_) == (local_ipv4_ & subnet_mask_) 
                             ? target_ip : default_gateway_ipv4_;

        uint8_t dst_mac[6];
        
        // CRITICAL FIX: L2 ARP Deadlock on UDP Broadcast/Multicast (RFC 1112)
        if (target_ip == 0xFFFFFFFF) {
            std::memset(dst_mac, 0xFF, 6); // Global Broadcast
        } else if ((target_ip & 0xFF) >= 224 && (target_ip & 0xFF) <= 239) {
            dst_mac[0] = 0x01; dst_mac[1] = 0x00; dst_mac[2] = 0x5E;
            dst_mac[3] = (target_ip >> 8) & 0x7F;
            dst_mac[4] = (target_ip >> 16) & 0xFF;
            dst_mac[5] = (target_ip >> 24) & 0xFF;
        } else if (SL_EXPECT_FALSE(!arp_cache_.resolve(next_hop_ip, dst_mac))) {
            // Stateless ARP Resolution Solicitation
            auto* out_arp = reinterpret_cast<raw_arp_frame*>(rte_pktmbuf_mtod(slot->mbuf, void*));
            std::memset(out_arp, 0, sizeof(raw_arp_frame));
            std::memset(out_arp->dest_mac, 0xFF, 6); // Global Broadcast
            __builtin_memcpy(out_arp->src_mac, local_mac_, 6);
            out_arp->eth_type = core::endian::host_to_network16(0x0806);
            out_arp->hw_type = core::endian::host_to_network16(1);
            out_arp->proto_type = core::endian::host_to_network16(0x0800);
            out_arp->hw_len = 6;
            out_arp->proto_len = 4;
            out_arp->opcode = core::endian::host_to_network16(1); // Request
            __builtin_memcpy(out_arp->sender_mac, local_mac_, 6);
            out_arp->sender_ip = local_ipv4_;
            std::memset(out_arp->target_mac, 0x00, 6);
            out_arp->target_ip = next_hop_ip;
            
            // The raw_arp_frame struct is 64 bytes and already zero-initialized,
            // naturally mitigating Etherleak without inducing a size_t underflow.
            slot->mbuf->data_len = 64;
            slot->mbuf->pkt_len = 64;
            gateway_egress_->commit_n(1);
            return false; // Dropped UDP payload, but sent ARP request natively
        }

        char* out_data = rte_pktmbuf_mtod(slot->mbuf, char*);
        auto* out_hdr = reinterpret_cast<raw_udp_ipv4_frame*>(out_data);

        __builtin_memcpy(out_hdr->dest_mac, dst_mac, 6);
        __builtin_memcpy(out_hdr->src_mac, local_mac_, 6);
        out_hdr->eth_type = core::endian::host_to_network16(0x0800);

        uint16_t ip_len = static_cast<uint16_t>(20 + 8 + payload_len);
        out_hdr->ip_ihl_ver = 0x45;
        out_hdr->ip_tos = 0;
        out_hdr->ip_len = core::endian::host_to_network16(ip_len);
        out_hdr->ip_id = 0;
        out_hdr->ip_frag_offset = core::endian::host_to_network16(0x4000); // DF
        out_hdr->ip_ttl = 64;
        out_hdr->ip_protocol = 17; // UDP
        out_hdr->ip_src = local_ipv4_;
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

    SLAB_HOT bool send_udp_ipv6(const uint64_t target_ipv6[2], uint16_t target_port, uint16_t source_port, const char* payload, size_t payload_len) noexcept {
        if (SL_EXPECT_FALSE(payload_len > 1452)) GW_DROP_FALSE("udp6 payload > 1452");
        if (SL_EXPECT_FALSE(payload == nullptr && payload_len > 0)) GW_DROP_FALSE("udp6 payload null");
        if (SL_EXPECT_FALSE(!gateway_egress_ || !tx_mbuf_pool_)) GW_DROP_FALSE("no egress or pool");

        auto* slot = gateway_egress_->get_reserved_slot(0);
        if (SL_EXPECT_FALSE(!slot)) GW_DROP_FALSE("no egress slot");

        slot->mbuf = rte_pktmbuf_alloc(tx_mbuf_pool_);
        if (SL_EXPECT_FALSE(!slot->mbuf)) GW_DROP_FALSE("mbuf alloc fail");

        uint8_t dst_mac[6];
        
        if ((reinterpret_cast<const uint8_t*>(&target_ipv6[0]))[0] == 0xFF) {
            dst_mac[0] = 0x33; dst_mac[1] = 0x33;
            dst_mac[2] = (reinterpret_cast<const uint8_t*>(&target_ipv6[1]))[4];
            dst_mac[3] = (reinterpret_cast<const uint8_t*>(&target_ipv6[1]))[5];
            dst_mac[4] = (reinterpret_cast<const uint8_t*>(&target_ipv6[1]))[6];
            dst_mac[5] = (reinterpret_cast<const uint8_t*>(&target_ipv6[1]))[7];
        } else if (SL_EXPECT_FALSE(!ndp_cache_.resolve(target_ipv6, dst_mac))) {
            char* out_data = rte_pktmbuf_mtod(slot->mbuf, char*);
            std::memset(out_data, 0, 86);
            
            out_data[0] = 0x33; out_data[1] = 0x33; out_data[2] = 0xFF;
            out_data[3] = (reinterpret_cast<const uint8_t*>(&target_ipv6[1]))[5];
            out_data[4] = (reinterpret_cast<const uint8_t*>(&target_ipv6[1]))[6];
            out_data[5] = (reinterpret_cast<const uint8_t*>(&target_ipv6[1]))[7];
            __builtin_memcpy(out_data + 6, local_mac_, 6);
            *reinterpret_cast<uint16_t*>(out_data + 12) = core::endian::host_to_network16(0x86DD);
            
            *reinterpret_cast<uint32_t*>(out_data + 14) = core::endian::host_to_network32(0x60000000);
            *reinterpret_cast<uint16_t*>(out_data + 18) = core::endian::host_to_network16(32);
            out_data[20] = 58; out_data[21] = 255;
            __builtin_memcpy(out_data + 22, local_ipv6_, 16);
            
            uint8_t snm[16] = {0xFF, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0xFF, static_cast<uint8_t>(out_data[3]), static_cast<uint8_t>(out_data[4]), static_cast<uint8_t>(out_data[5])};
            __builtin_memcpy(out_data + 38, snm, 16);
            
            out_data[54] = 135; out_data[55] = 0;
            __builtin_memcpy(out_data + 62, target_ipv6, 16);
            out_data[78] = 1; out_data[79] = 1;
            __builtin_memcpy(out_data + 80, local_mac_, 6);
            
            uint32_t pseudo_sum = 0;
            for(int i=0; i<8; ++i) pseudo_sum += reinterpret_cast<const uint16_t*>(local_ipv6_)[i];
            for(int i=0; i<8; ++i) pseudo_sum += reinterpret_cast<const uint16_t*>(snm)[i];
            pseudo_sum += core::endian::host_to_network16(58);
            pseudo_sum += core::endian::host_to_network16(32);
            *reinterpret_cast<uint16_t*>(out_data + 56) = tcp_wire_engine::compute_checksum(out_data + 54, 32, pseudo_sum);
            
            slot->mbuf->data_len = 86;
            slot->mbuf->pkt_len = 86;
            gateway_egress_->commit_n(1);
            return false;
        }
        
        char* out_data = rte_pktmbuf_mtod(slot->mbuf, char*);
        auto* out_hdr = reinterpret_cast<raw_udp_ipv6_frame*>(out_data);
        
        __builtin_memcpy(out_hdr->dest_mac, dst_mac, 6);
        __builtin_memcpy(out_hdr->src_mac, local_mac_, 6);
        out_hdr->eth_type = core::endian::host_to_network16(0x86DD);
        
        out_hdr->ipv6_flow = core::endian::host_to_network32(0x60000000);
        out_hdr->ipv6_plen = core::endian::host_to_network16(8 + payload_len);
        out_hdr->ipv6_nxt = 17;
        out_hdr->ipv6_hlim = 64;
        out_hdr->ipv6_src[0] = local_ipv6_[0];
        out_hdr->ipv6_src[1] = local_ipv6_[1];
        out_hdr->ipv6_dst[0] = target_ipv6[0];
        out_hdr->ipv6_dst[1] = target_ipv6[1];
        
        out_hdr->udp_src_port = core::endian::host_to_network16(source_port);
        out_hdr->udp_dst_port = core::endian::host_to_network16(target_port);
        out_hdr->udp_length = core::endian::host_to_network16(8 + payload_len);
        out_hdr->udp_checksum = 0;
        
        if (payload_len > 0) {
            std::memcpy(out_data + 62, payload, payload_len);
        }
        
        uint32_t pseudo_sum = 0;
        for(int i=0; i<8; ++i) pseudo_sum += reinterpret_cast<const uint16_t*>(out_hdr->ipv6_src)[i];
        for(int i=0; i<8; ++i) pseudo_sum += reinterpret_cast<const uint16_t*>(out_hdr->ipv6_dst)[i];
        pseudo_sum += core::endian::host_to_network16(17);
        pseudo_sum += out_hdr->udp_length;
        
        uint16_t csum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(out_hdr) + 54, 8 + payload_len, pseudo_sum);
        out_hdr->udp_checksum = (csum == 0) ? 0xFFFF : csum;
        
        uint32_t final_len = 54 + 8 + payload_len;
        if (SL_EXPECT_FALSE(final_len < 60)) {
            std::memset(out_data + final_len, 0, 60 - final_len);
            final_len = 60;
        }
        slot->mbuf->data_len = final_len;
        slot->mbuf->pkt_len = final_len;
        
        gateway_egress_->commit_n(1);
        return true;
    }

        SLAB_FORCE_INLINE void stateless_syn_ack(raw_tcp_ipv4_frame& out_hdr, const raw_tcp_ipv4_frame& in_hdr, uint32_t cookie, uint64_t current_time_ms, uint8_t wscale, uint8_t sack, bool ts, uint32_t client_tsval) noexcept {
            __builtin_memcpy(out_hdr.dest_mac, in_hdr.src_mac, 6);
            __builtin_memcpy(out_hdr.src_mac, local_mac_, 6);
            out_hdr.eth_type = in_hdr.eth_type;

            uint8_t opt_len = 12;
            if (ts) opt_len += 12;

            out_hdr.ip_ihl_ver = 0x45;
            out_hdr.ip_tos = 0;
            out_hdr.ip_len = core::endian::network_to_host16(40 + opt_len);
            out_hdr.ip_id = 0;
            out_hdr.ip_frag_offset = core::endian::network_to_host16(0x4000);
            out_hdr.ip_ttl = 64;
            out_hdr.ip_protocol = 6;
            out_hdr.ip_src = local_ipv4_;
            out_hdr.ip_dst = in_hdr.ip_src;
            out_hdr.ip_checksum = 0; // Hardware handles L3 CKSUM
            out_hdr.ip_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(&out_hdr) + 14, 20, 0);
            
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
                // Linux-style stateless capability encoding into TSval
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

        SLAB_FORCE_INLINE void stateless_rst_ack(raw_tcp_ipv4_frame& out_hdr, const raw_tcp_ipv4_frame& in_hdr) noexcept {
            __builtin_memcpy(out_hdr.dest_mac, in_hdr.src_mac, 6);
            __builtin_memcpy(out_hdr.src_mac, local_mac_, 6);
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
            out_hdr.ip_checksum = 0; // Hardware handles L3 CKSUM
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

        SLAB_FORCE_INLINE void stateless_syn_ack_ipv6(raw_tcp_ipv6_frame& out_hdr, const raw_tcp_ipv6_frame& in_hdr, uint32_t cookie, uint64_t current_time_ms, uint8_t wscale, uint8_t sack, bool ts, uint32_t client_tsval) noexcept {
            __builtin_memcpy(out_hdr.dest_mac, in_hdr.src_mac, 6);
            __builtin_memcpy(out_hdr.src_mac, local_mac_, 6);
            out_hdr.eth_type = in_hdr.eth_type;

            uint8_t opt_len = 12;
            if (ts) opt_len += 12;

            out_hdr.ipv6_flow = core::endian::host_to_network32(0x60000000);
            out_hdr.ipv6_plen = core::endian::host_to_network16(20 + opt_len);
            out_hdr.ipv6_nxt = 6;
            out_hdr.ipv6_hlim = 64;
            out_hdr.ipv6_src[0] = local_ipv6_[0];
            out_hdr.ipv6_src[1] = local_ipv6_[1];
            out_hdr.ipv6_dst[0] = in_hdr.ipv6_src[0];
            out_hdr.ipv6_dst[1] = in_hdr.ipv6_src[1];

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

        SLAB_FORCE_INLINE void stateless_rst_ack_ipv6(raw_tcp_ipv6_frame& out_hdr, const raw_tcp_ipv6_frame& in_hdr) noexcept {
            __builtin_memcpy(out_hdr.dest_mac, in_hdr.src_mac, 6);
            __builtin_memcpy(out_hdr.src_mac, local_mac_, 6);
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

        // Lock-free connection map traversal
        hash_node* lookup_node(const tcp_4tuple& tuple) noexcept {
            uint32_t hash = tcp_4tuple_hash::hash(tuple) % NUM_BUCKETS;
            hash_node* node = buckets_[hash].load(std::memory_order_relaxed);
            while (node) {
                if (node->key == tuple) return node;
                node = node->next.load(std::memory_order_relaxed);
            }

            // CRITICAL FIX: Bidirectional Tuple Lookup for Simultaneous Open & Outbound Replies
            tcp_4tuple reversed_tuple{tuple.dst_ip, tuple.src_ip, tuple.dst_port, tuple.src_port};
            hash = tcp_4tuple_hash::hash(reversed_tuple) % NUM_BUCKETS;
            node = buckets_[hash].load(std::memory_order_relaxed);
            while (node) {
                if (node->key == reversed_tuple) return node;
                node = node->next.load(std::memory_order_relaxed);
            }
            return nullptr; // No match in either direction
        }

        // Safe dynamic acquisition from the memory pool
        hash_node* create_connection(const tcp_4tuple& tuple) noexcept {
            tcp_transmission_control_block* tcb = tcb_pool_.make_raw();
            if (SL_EXPECT_FALSE(!tcb)) return nullptr;
            std::memset(tcb, 0, sizeof(tcp_transmission_control_block));
            tcb->phase_mask = PHASE_LISTEN;

            hash_node* new_node = node_pool_.make_raw();
            if (SL_EXPECT_FALSE(!new_node)) {
                tcb_pool_.release(tcb);
                return nullptr;
            }
            new_node->key = tuple;
            new_node->tcb = tcb;
            new_node->conn_id = next_conn_id_.fetch_add(1, std::memory_order_relaxed);
            
            uint32_t hash = tcp_4tuple_hash::hash(tuple) % NUM_BUCKETS;
            hash_node* expected = buckets_[hash].load(std::memory_order_relaxed);
            do {
                new_node->next.store(expected, std::memory_order_relaxed);
            } while (!buckets_[hash].compare_exchange_weak(expected, new_node, std::memory_order_release, std::memory_order_relaxed));

            return new_node;
        }

        // Expose TCB for diagnostic and testing integrations
        tcp_transmission_control_block& get_tcb(uint32_t conn_id) noexcept {
            for (size_t i = 0; i < NUM_BUCKETS; ++i) {
                hash_node* node = buckets_[i].load(std::memory_order_relaxed);
                while (node) {
                    if (node->conn_id == conn_id) return *node->tcb;
                    node = node->next.load(std::memory_order_relaxed);
                }
            }
            static tcp_transmission_control_block dummy{};
            return dummy;
        }

        /**
         * @brief Natively allocates an ephemeral port, registers the connection, and initiates an Active Open.
         * @return The established connection_id, or 0 if allocation failed.
         */
        SLAB_HOT uint32_t connect_outbound(uint32_t target_ip, uint16_t target_port) noexcept {
            for (int i = 0; i < 16384; ++i) {
                uint32_t p = ephemeral_port_cursor_.fetch_add(1, std::memory_order_relaxed);
                uint16_t port = 49152 + (p % 16384); // IANA standard ephemeral range

                // Structural Geometric Invariant: Inbound packets map source as remote and destination as local. 
                // Connections MUST be anchored symmetrically to the inbound tuple perspective to prevent hash partition misses.
                tcp_4tuple tuple{target_ip, local_ipv4_, core::endian::host_to_network16(target_port), core::endian::host_to_network16(port)};
                if (lookup_node(tuple) != nullptr) continue; // Port collision, try next

                hash_node* conn_node = create_connection(tuple);
                if (SL_EXPECT_FALSE(!conn_node)) return 0; // TCB Pool exhausted

                tcp_transmission_control_block& tcb = *conn_node->tcb;
                tcb.phase_mask = PHASE_SYN_SENT;
                tcb.local_ipv4 = local_ipv4_;
                tcb.remote_ipv4 = target_ip;
                tcb.local_port = tuple.dst_port;
                tcb.remote_port = tuple.src_port;
                
                // RFC 6528: Cryptographically Secure ISN Generation
                uint32_t isn = tcp_syn_cookie::compute_mac(tcb.local_ipv4, tcb.local_port, tcb.remote_ipv4, tcb.remote_port, 0, tcp_syn_cookie::get_time_counter());
                tcb.snd_una = isn;
                tcb.snd_nxt = isn;
                tcb.tx_mbuf_pool = tx_mbuf_pool_;
                tcb.tx_egress_conduit = gateway_egress_;
                tcb.tx_unacked_ring = nullptr; // Unacked ring is bound by the Application Layer
                tcb.snd_wnd = 65535;
                tcb.rcv_wnd = 65535;
                tcb.cwnd = 1460u * 10;
                tcb.ssthresh = 65535;
                tcb.ecn_permitted = 1;
                tcb.temporal_flags |= TEMP_FLAG_ARP_WAIT;

                return conn_node->conn_id;
            }
            return 0; // Exhausted available ephemeral ports
        }

        SLAB_HOT uint32_t connect_outbound_ipv6(const uint64_t target_ipv6[2], uint16_t target_port) noexcept {
            for (int i = 0; i < 16384; ++i) {
                uint32_t p = ephemeral_port_cursor_.fetch_add(1, std::memory_order_relaxed);
                uint16_t port = 49152 + (p % 16384);

                tcp_4tuple tuple{};
                tuple.src_ipv6[0] = target_ipv6[0];
                tuple.src_ipv6[1] = target_ipv6[1];
                tuple.dst_ipv6[0] = local_ipv6_[0];
                tuple.dst_ipv6[1] = local_ipv6_[1];
                tuple.src_port = core::endian::host_to_network16(target_port);
                tuple.dst_port = core::endian::host_to_network16(port);
                tuple.is_ipv6 = 1;
                
                if (lookup_node(tuple) != nullptr) continue;

                hash_node* conn_node = create_connection(tuple);
                if (SL_EXPECT_FALSE(!conn_node)) return 0;

                tcp_transmission_control_block& tcb = *conn_node->tcb;
                tcb.phase_mask = PHASE_SYN_SENT;
                tcb.local_ipv6[0] = local_ipv6_[0];
                tcb.local_ipv6[1] = local_ipv6_[1];
                tcb.remote_ipv6[0] = target_ipv6[0];
                tcb.remote_ipv6[1] = target_ipv6[1];
                tcb.local_port = tuple.dst_port;
                tcb.remote_port = tuple.src_port;
                tcb.is_ipv6 = 1;
                
                uint32_t isn = tcp_syn_cookie::generate_ipv6(tcb.local_ipv6, tcb.local_port, tcb.remote_ipv6, tcb.remote_port, 0, 1220, tcp_syn_cookie::get_time_counter());
                tcb.snd_una = isn;
                tcb.snd_nxt = isn;
                tcb.tx_mbuf_pool = tx_mbuf_pool_;
                tcb.tx_egress_conduit = gateway_egress_;
                tcb.snd_wnd = 65535;
                tcb.rcv_wnd = 65535;
                tcb.cwnd = 1460u * 10;
                tcb.ssthresh = 65535;
                tcb.ecn_permitted = 1;
                tcb.temporal_flags |= TEMP_FLAG_ARP_WAIT;

                return conn_node->conn_id;
            }
            return 0;
        }

        SLAB_HOT void on_raw_frame(const char* data, size_t length, uint64_t current_time_ms, struct rte_mbuf* mbuf = nullptr) noexcept {
            mbuf_cleanup_guard mbuf_guard{mbuf};

            if (SL_EXPECT_FALSE(length < 42)) GW_DROP("length < 42"); // Support ARP/UDP/ICMP minimums

            uint16_t eth_type = core::endian::network_to_host16(*reinterpret_cast<const uint16_t*>(data + 12));
            
            if (SL_EXPECT_FALSE(eth_type == 0x86DD)) {
                // IPv6 Substrate Routing Boundary
                on_raw_frame_ipv6(data, length, current_time_ms, mbuf);
                mbuf_guard.absorbed = true; // Transferred ownership to the native IPv6 handler
                return;
            }

            // VLAN 802.1Q Tag Stripping (Arbitrary depth for Data Center Q-in-Q support)
            while (SL_EXPECT_FALSE(eth_type == 0x8100 || eth_type == 0x88A8)) {
                if (SL_EXPECT_FALSE(length < 58)) GW_DROP("vlan trunc");
                // Shift the 14-byte Ethernet header right by 4 bytes to overwrite the VLAN tag
                std::memmove(const_cast<char*>(data) + 4, data, 12);
                data += 4;
                length -= 4;
                eth_type = core::endian::network_to_host16(*reinterpret_cast<const uint16_t*>(data + 12));
                if (mbuf) {
                    mbuf->data_off += 4;
                    mbuf->data_len -= 4;
                    mbuf->pkt_len -= 4;
                }
            }

            if (SL_EXPECT_FALSE(eth_type == 0x0806)) { // L2 Address Resolution Protocol
                if (length < sizeof(raw_arp_frame)) GW_DROP("arp trunc");
                auto* arp = reinterpret_cast<const raw_arp_frame*>(data);
                
                if (core::endian::network_to_host16(arp->opcode) == 1) { // Broadcast Request
                    // CRITICAL FIX: ARP Reflection Storm Prevention
                    if (SL_EXPECT_FALSE(arp->sender_mac[0] & 1)) GW_DROP("arp req mcast mac");

                    if (arp->target_ip == local_ipv4_) {
                        if (SL_EXPECT_TRUE(gateway_egress_ && tx_mbuf_pool_)) {
                            auto* slot = gateway_egress_->get_reserved_slot(0);
                            if (slot) {
                                slot->mbuf = rte_pktmbuf_alloc(tx_mbuf_pool_);
                                if (slot->mbuf) {
                                    auto* out_arp = reinterpret_cast<raw_arp_frame*>(rte_pktmbuf_mtod(slot->mbuf, void*));
                                    std::memset(out_arp, 0, sizeof(raw_tcp_ipv4_frame));
                                    __builtin_memcpy(out_arp->dest_mac, arp->sender_mac, 6);
                                    __builtin_memcpy(out_arp->src_mac, local_mac_, 6);
                                    out_arp->eth_type = core::endian::host_to_network16(0x0806);
                                    out_arp->hw_type = core::endian::host_to_network16(1);
                                    out_arp->proto_type = core::endian::host_to_network16(0x0800);
                                    out_arp->hw_len = 6;
                                    out_arp->proto_len = 4;
                                    out_arp->opcode = core::endian::host_to_network16(2); // Reply
                                    __builtin_memcpy(out_arp->sender_mac, local_mac_, 6);
                                    out_arp->sender_ip = local_ipv4_;
                                    __builtin_memcpy(out_arp->target_mac, arp->sender_mac, 6);
                                    out_arp->target_ip = arp->sender_ip;
                                    
                                    // The raw_arp_frame struct is 64 bytes and already zero-initialized.
                                    slot->mbuf->data_len = 64;
                                    slot->mbuf->pkt_len = 64;
                                    gateway_egress_->commit_n(1);
                                }
                            }
                        }
                    }
                } else if (core::endian::network_to_host16(arp->opcode) == 2) { // Reply
                    // CRITICAL FIX: ARP Broadcast Storm Poisoning (L2 Amplification)
                    if (SL_EXPECT_FALSE(arp->sender_mac[0] & 1)) GW_DROP("arp rep mcast mac");

                    if (arp->target_ip == local_ipv4_ && arp->sender_ip != local_ipv4_) {
                        arp_cache_.insert(arp->sender_ip, arp->sender_mac, current_time_ms);
                    }
                }
                return;
            }

            if (SL_EXPECT_FALSE(eth_type != 0x0800)) GW_DROP("not ipv4");

            auto* hdr = reinterpret_cast<const raw_tcp_ipv4_frame*>(data);

            // CRITICAL FIX: IP Options L4 Shift Hijacking & LAND Attack Prevention
            // The Linux kernel validates IHL and drops symmetric routing loops natively.
            if (SL_EXPECT_FALSE((hdr->ip_ihl_ver & 0x0F) != 5)) GW_DROP("ip options"); 
            if (SL_EXPECT_FALSE(hdr->ip_src == hdr->ip_dst || hdr->ip_src == local_ipv4_)) GW_DROP("land attack");
            
            // CRITICAL FIX: Topological Poisoning (Broadcast/Multicast Reflection)
            if (SL_EXPECT_FALSE((hdr->ip_src & 0xFF) >= 224)) GW_DROP("mcast src");
            if (SL_EXPECT_FALSE(hdr->ip_src == 0xFFFFFFFF)) GW_DROP("bcast src");

            // CRITICAL FIX: Promiscuous Mode MAC Poisoning (L2 Side-Channel Drop)
            // OS-bypass NICs receive all frames. We MUST drop frames addressed to other switch tenants.
            if (SL_EXPECT_FALSE((hdr->dest_mac[0] & 1) == 0)) { // Unicast Frame
                if (SL_EXPECT_FALSE(__builtin_memcmp(hdr->dest_mac, local_mac_, 6) != 0)) GW_DROP("wrong dest mac");
            }

            // CRITICAL FIX: Martian Packet & Loopback Spoofing (CVE-1999-0186)
            if (SL_EXPECT_FALSE((hdr->ip_src & 0xFF) == 127 || (hdr->ip_src & 0xFF) == 0)) GW_DROP("martian ip");

            // CRITICAL FIX: TCB IP Spoofing & Open Relay Hijack
            if (SL_EXPECT_FALSE(hdr->ip_dst != local_ipv4_ && hdr->ip_dst != 0xFFFFFFFF)) GW_DROP("wrong dst ip");

        // CRITICAL FIX: L3 Checksum Evasion (Hardware Agnostic)
        // Checksums MUST be validated before branching into UDP/ICMP logic to prevent 
        // corrupted headers from bypassing protocol verification and poisoning L7.
        uint16_t ip_csum = tcp_wire_engine::compute_checksum(reinterpret_cast<const char*>(hdr) + 14, 20, 0);
            if (SL_EXPECT_FALSE(ip_csum != 0 && ip_csum != 0xFFFF)) GW_DROP("ip csum fail: " << ip_csum);

        uint16_t frag;
            __builtin_memcpy(&frag, &hdr->ip_frag_offset, 2);
            if (SL_EXPECT_FALSE((__builtin_bswap16(frag) & 0x3FFFu) != 0)) {
                // Complete Socket Replacement: Native IP Defragmentation without dynamic allocation
                hdr = ip_defrag_.process_fragment(hdr, length, current_time_ms);
                if (!hdr) GW_DROP("still defrag"); // Still reassembling
                
                // Re-evaluate logical length parameters for the newly reassembled datagram
            // The data pointer and length will be re-evaluated to point to the complete datagram.
                length = 14 + core::endian::network_to_host16(hdr->ip_len);
                data = reinterpret_cast<const char*>(hdr);
                
                // Hardware Buffer Synthesis: Replace partial fragment mbuf with a contiguous reassembled mbuf
                if (SL_EXPECT_TRUE(tx_mbuf_pool_)) {
                    // CRITICAL FIX: Prevent DPDK mbuf overflow on jumbo reassembly
                    if (SL_EXPECT_FALSE(length > 2048 - 128)) { // Safely bound to standard 2KB mbuf geometry
                        GW_DROP("jumbo frag");
                    }

                    struct rte_mbuf* new_mbuf = rte_pktmbuf_alloc(tx_mbuf_pool_);
                    if (SL_EXPECT_TRUE(new_mbuf)) {
                        char* mbuf_data = rte_pktmbuf_mtod(new_mbuf, char*);
                        std::memcpy(mbuf_data, data, length);
                        new_mbuf->data_len = length;
                        new_mbuf->pkt_len = length;
                        if (mbuf) rte_pktmbuf_free(mbuf); // Free the final fragment's buffer
                        mbuf = new_mbuf; // Guard automatically adopts the synthesized buffer!
                        data = mbuf_data;
                        hdr = reinterpret_cast<const raw_tcp_ipv4_frame*>(data);
                    } else {
                        GW_DROP("pool exhaust");
                    }
                } else {
                    if (mbuf) rte_pktmbuf_free(mbuf);
                    mbuf = nullptr; // Prevent final fragment mbuf from being smuggled into OOO matrix!
                }
            }

            // CRITICAL FIX: Ethernet Padding Payload Corruption
            // We MUST use the logical IP length to extract L7 payload, ignoring physical L2 padding.
            // This must be performed AFTER defragmentation to ensure reassembled payloads are parsed.
            uint32_t logical_len = 14 + core::endian::network_to_host16(hdr->ip_len);
            if (SL_EXPECT_FALSE(length < logical_len)) GW_DROP("hw trunc");
            uint16_t ip_hlen = (hdr->ip_ihl_ver & 0x0F) * 4;
            if (SL_EXPECT_FALSE(logical_len < 14u + ip_hlen)) GW_DROP("ip len underflow");

            if (SL_EXPECT_FALSE(hdr->ip_protocol == 1)) { // ICMP Protocol Handling
                if (logical_len >= 14u + ip_hlen + 8u) { 
                    uint8_t icmp_type = data[14 + ip_hlen];
                    if (icmp_type == 8 && hdr->ip_dst == local_ipv4_) { // Echo Request (Ping)
                        // CRITICAL FIX: ICMP Checksum Oracle (Amplification Vector)
                        // Do not act as a free checksum generator for corrupted packets
                        uint32_t icmp_len = logical_len - 14 - ip_hlen;
                        uint16_t icmp_csum = tcp_wire_engine::compute_checksum(data + 14 + ip_hlen, icmp_len, 0);
                        if (SL_EXPECT_FALSE(icmp_csum != 0 && icmp_csum != 0xFFFF)) GW_DROP("icmp csum fail");

                        // CRITICAL FIX: The "Mongolian Ping of Death" (CVE-1999-0128 / Jumbo LRO Mempool Overflow)
                        // DPDK mempools strictly bound mbuf sizes (typically 2048 bytes).
                        // If an attacker sends an ICMP Echo Request larger than the physical Ethernet MTU,
                        // std::memcpy will violently overflow the mbuf, corrupting the TX ring and crashing the gateway.
                        if (SL_EXPECT_FALSE(logical_len > 1514)) GW_DROP("icmp oversized");

                        if (SL_EXPECT_TRUE(gateway_egress_ && tx_mbuf_pool_)) {
                            auto* slot = gateway_egress_->get_reserved_slot(0);
                            if (slot) {
                                slot->mbuf = rte_pktmbuf_alloc(tx_mbuf_pool_);
                                if (slot->mbuf) {
                                    char* out_data = rte_pktmbuf_mtod(slot->mbuf, char*);
                                    std::memcpy(out_data, data, logical_len);
                                    
                                    auto* out_hdr = reinterpret_cast<raw_tcp_ipv4_frame*>(out_data);
                                    __builtin_memcpy(out_hdr->dest_mac, hdr->src_mac, 6);
                                    __builtin_memcpy(out_hdr->src_mac, local_mac_, 6);
                                    out_hdr->ip_dst = hdr->ip_src;
                                    out_hdr->ip_src = local_ipv4_;
                                    
                                    out_data[14 + ip_hlen] = 0; // ICMP Type: Echo Reply
                                    *reinterpret_cast<uint16_t*>(&out_data[14 + ip_hlen + 2]) = 0; // Reset checksum
                                    *reinterpret_cast<uint16_t*>(&out_data[14 + ip_hlen + 2]) = tcp_wire_engine::compute_checksum(&out_data[14 + ip_hlen], icmp_len, 0);
                                    
                                    // CRITICAL FIX: Etherleak Padding
                                    uint32_t final_len = logical_len;
                                    if (SL_EXPECT_FALSE(final_len < 60)) {
                                        std::memset(out_data + final_len, 0, 60 - final_len);
                                        final_len = 60;
                                    }
                                    slot->mbuf->data_len = final_len;
                                    slot->mbuf->pkt_len = final_len;
                                    gateway_egress_->commit_n(1);
                                }
                            }
                        }
                    } else if (icmp_type == 3 && logical_len >= 14u + ip_hlen + 8u + 20u) { // Destination Unreachable + Inner IP
                        uint8_t icmp_code = data[14 + ip_hlen + 1];
                        if (icmp_code == 4) { // Fragmentation Needed (PMTUD)
                            uint16_t next_hop_mtu = core::endian::network_to_host16(*reinterpret_cast<const uint16_t*>(&data[14 + ip_hlen + 6]));
                            const auto* orig_ip = reinterpret_cast<const raw_tcp_ipv4_frame*>(&data[14 + ip_hlen + 8 - 14]);
                            if (orig_ip->ip_protocol == 6 && logical_len >= 14u + ip_hlen + 8u + ((orig_ip->ip_ihl_ver & 0x0F) * 4) + 8u) {
                                const uint16_t* orig_ports = reinterpret_cast<const uint16_t*>(&data[14 + ip_hlen + 8 + ((orig_ip->ip_ihl_ver & 0x0F) * 4)]);
                                tcp_4tuple tuple{orig_ip->ip_dst, orig_ip->ip_src, orig_ports[1], orig_ports[0]};
                                hash_node* conn_node = lookup_node(tuple);
                                if (conn_node) {
                                    // CVE-2005-0068: Validate inner TCP Sequence Number to prevent Blind PMTUD Spoofing
                                    uint32_t inner_seq = core::endian::network_to_host32(*reinterpret_cast<const uint32_t*>(&data[14 + ip_hlen + 8 + ((orig_ip->ip_ihl_ver & 0x0F) * 4) + 4]));
                                    tcp_transmission_control_block& tcb = *conn_node->tcb;
                                    uint32_t inflight = tcb.snd_nxt - tcb.snd_una;
                                    if (inner_seq - tcb.snd_una <= inflight) {
                                        uint16_t new_mss = (next_hop_mtu > 40) ? (next_hop_mtu - 40) : 536;
                                        if (new_mss < tcb.remote_mss) tcb.remote_mss = new_mss;
                                    }
                                }
                            }
                        }
                    }
                }
            return;
            }

            if (SL_EXPECT_FALSE(hdr->ip_protocol != 6)) {
                if (hdr->ip_protocol == 17 && logical_len >= 14u + ip_hlen + 8u) { // UDP Protocol Handling
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
                if (SL_EXPECT_FALSE(logical_len < 14u + ip_hlen + udp_len)) GW_DROP("udp bounds");
                if (SL_EXPECT_FALSE(udp_dst_port == 0)) GW_DROP("udp zero port");

                    uint32_t pseudo_sum = 0;
                uint32_t src = hdr->ip_src;
                uint32_t dst = hdr->ip_dst;
                pseudo_sum += (src & 0xFFFF) + (src >> 16);
                pseudo_sum += (dst & 0xFFFF) + (dst >> 16);
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
                            if (defragmenter_.on_udp(frame)) {
                                return; // Successfully handled by L7 application
                            }
                        }
                        
                        // CRITICAL FIX: ICMP Smurf Amplification (RFC 1122)
                        if (SL_EXPECT_FALSE(hdr->ip_dst != local_ipv4_)) GW_DROP("udp smurf");

                        // ICMP Rate Limiting to prevent Smurf/Amplification DDoS
                        if (SL_EXPECT_FALSE(current_time_ms != last_icmp_reset_ms_)) {
                            last_icmp_reset_ms_ = current_time_ms;
                            icmp_tokens_ = 10; // Rate limit to ~10k ICMP packets per second
                        }
                    if (SL_EXPECT_FALSE(icmp_tokens_ == 0)) GW_DROP("icmp limit");
                        icmp_tokens_--;

                        if (SL_EXPECT_TRUE(gateway_egress_ && tx_mbuf_pool_)) {
                            auto* slot = gateway_egress_->get_reserved_slot(0);
                            if (slot) {
                                slot->mbuf = rte_pktmbuf_alloc(tx_mbuf_pool_);
                                if (slot->mbuf) {
                                    char* out_data = rte_pktmbuf_mtod(slot->mbuf, char*);
                                    
                                    uint16_t orig_payload_len = ip_hlen + 8; // Full IP header + 8 bytes UDP
                                    uint16_t icmp_total_len = 8 + orig_payload_len; // ICMP Header + Payload
                                    uint16_t outer_ip_len = 20 + icmp_total_len;
                                    uint32_t total_frame_len = 14 + outer_ip_len;

                                    std::memset(out_data, 0, total_frame_len);
                                    auto* out_hdr = reinterpret_cast<raw_tcp_ipv4_frame*>(out_data);
                                    
                                    __builtin_memcpy(out_hdr->dest_mac, hdr->src_mac, 6);
                                    __builtin_memcpy(out_hdr->src_mac, local_mac_, 6);
                                    out_hdr->eth_type = hdr->eth_type;
                                    out_hdr->ip_ihl_ver = 0x45;
                                    out_hdr->ip_tos = 0;
                                    out_hdr->ip_len = core::endian::host_to_network16(outer_ip_len);
                                    out_hdr->ip_id = 0;
                                    out_hdr->ip_frag_offset = core::endian::host_to_network16(0x4000); // DF
                                    out_hdr->ip_ttl = 64;
                                    out_hdr->ip_protocol = 1; // ICMP
                                    out_hdr->ip_src = local_ipv4_;
                                    out_hdr->ip_dst = hdr->ip_src;
                                    out_hdr->ip_checksum = 0; // Hardware handles L3 CKSUM
                                    
                                    out_data[34] = 3; // ICMP Type: Destination Unreachable
                                    out_data[35] = 3; // ICMP Code: Port Unreachable
                                    out_data[36] = 0; out_data[37] = 0; // Checksum placeholder
                                    out_data[38] = 0; out_data[39] = 0; out_data[40] = 0; out_data[41] = 0; // Unused
                                    
                                    std::memcpy(&out_data[42], &data[14], orig_payload_len); // Copy full original IP header + 8 bytes UDP
                                    
                                    *reinterpret_cast<uint16_t*>(&out_data[36]) = tcp_wire_engine::compute_checksum(&out_data[34], icmp_total_len, 0);
                                    
                                    uint32_t final_len = total_frame_len;
                                    if (SL_EXPECT_FALSE(final_len < 60)) {
                                        std::memset(out_data + final_len, 0, 60 - final_len);
                                        final_len = 60;
                                    }
                                    slot->mbuf->data_len = final_len;
                                    slot->mbuf->pkt_len = final_len;
                                    gateway_egress_->commit_n(1);
                                }
                            }
                        }
                    }
                }
                return;
            }

            // CRITICAL FIX: Port 0 Multiplexing Filter
            if (SL_EXPECT_FALSE(hdr->tcp_src_port == 0 || hdr->tcp_dst_port == 0)) GW_DROP("tcp zero port");

            if (SL_EXPECT_FALSE(logical_len < 14u + ip_hlen + 20u)) GW_DROP("tcp short");

            // RFC Option Parsing & Payload Bounding
            uint8_t data_offset_words = hdr->tcp_data_offset >> 4;
            if (SL_EXPECT_FALSE(data_offset_words < 5)) GW_DROP("tcp offset < 5");
            uint32_t header_size = 14 + ip_hlen + (data_offset_words * 4);
            if (SL_EXPECT_FALSE(logical_len < header_size)) GW_DROP("tcp offset bounds");
            
            // CRITICAL FIX: TCP Flag Anomaly Scans (XMAS, NULL, SYN-FIN)
            // Drop illegal flag combinations commonly used for OS fingerprinting and firewall bypass.
            if (SL_EXPECT_FALSE((hdr->tcp_flags & (FLAG_SYN | FLAG_FIN)) == (FLAG_SYN | FLAG_FIN))) GW_DROP("tcp syn-fin");
            if (SL_EXPECT_FALSE(!(hdr->tcp_flags & (FLAG_SYN | FLAG_ACK | FLAG_RST)))) GW_DROP("tcp bad flags");

            // Software RX Checksum Verification Fallback (Protects against Cloud NIC / Virtio corruption)
            uint16_t tcp_len = logical_len - 14 - ip_hlen;
            uint32_t pseudo_sum = 0;
        uint32_t src = hdr->ip_src;
        uint32_t dst = hdr->ip_dst;
        pseudo_sum += (src & 0xFFFF) + (src >> 16);
        pseudo_sum += (dst & 0xFFFF) + (dst >> 16);
            pseudo_sum += core::endian::host_to_network16(6);
            pseudo_sum += core::endian::host_to_network16(tcp_len);
            
            uint16_t tcp_csum = tcp_wire_engine::compute_checksum(reinterpret_cast<const char*>(hdr) + 14 + ip_hlen, tcp_len, pseudo_sum);
            if (SL_EXPECT_FALSE(tcp_csum != 0 && tcp_csum != 0xFFFF)) GW_DROP("tcp csum fail: " << tcp_csum);

            tcp_4tuple tuple{hdr->ip_src, hdr->ip_dst, hdr->tcp_src_port, hdr->tcp_dst_port};
            hash_node* conn_node = lookup_node(tuple);

            uint32_t payload_len = logical_len - header_size;
            uint32_t original_payload_len = payload_len;
            const char* payload_ptr = data + header_size;

            if (!conn_node) {
                // Zero-Allocation SYN-Flood Protection (SYN Cookies)
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
                            if (kind == 2 && len == 4) { 
                                client_mss = (opt_ptr[2] << 8) | opt_ptr[3]; 
                                if (SL_EXPECT_FALSE(client_mss < 536)) client_mss = 536;
                            }
                            else if (kind == 3 && len == 3) { client_wscale = opt_ptr[2] > 14 ? 14 : opt_ptr[2]; }
                            else if (kind == 4 && len == 2) { client_sack = 1; }
                            else if (kind == 8 && len == 10) { 
                                client_ts = true; 
                                client_tsval = core::endian::network_to_host32(*reinterpret_cast<const uint32_t*>(opt_ptr + 2)); 
                            }                            opt_ptr += len; bytes_left -= len;
                        }
                    }
                    
                    uint32_t cookie = tcp_syn_cookie::generate(hdr->ip_src, hdr->tcp_src_port, hdr->ip_dst, hdr->tcp_dst_port, core::endian::network_to_host32(hdr->tcp_seq), client_mss, current_time_ms);
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
                    return;
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

                    auto status = tcp_syn_cookie::validate(hdr->ip_src, hdr->tcp_src_port, hdr->ip_dst, hdr->tcp_dst_port, core::endian::network_to_host32(hdr->tcp_ack) - 1, core::endian::network_to_host32(hdr->tcp_seq) - 1, current_time_ms);
                    SLAB_GW_DEBUG("Inbound ACK: Cookie validation result=" << (status.is_valid ? "PASS" : "FAIL"));
                    if (status.is_valid) {
                        conn_node = lookup_node(tuple);
                        if (conn_node) SLAB_GW_DEBUG("Inbound ACK: Matched existing connection " << conn_node->conn_id);
                        if (SL_EXPECT_FALSE(conn_node)) GW_DROP("dup conn node");

                    if (SL_EXPECT_FALSE(!accept_queue_)) GW_DROP("no accept queue");
                        // CRITICAL FIX: Accept Queue Overflow detection.
                        // We first attempt to reserve a slot. If that fails, or if the ring
                        // explicitly reports no available push space, we trigger overflow.
                        // If get_reserved_slot() does not return nullptr when full, we must use available_to_peek()
                        // to detect saturation.
                        uint32_t* accept_slot = accept_queue_->get_reserved_slot(0); // Attempt to reserve a slot
                        if (SL_EXPECT_FALSE(!accept_slot || accept_queue_->available_to_peek() >= 1023)) { // Check if ring is full
                            SLAB_GW_DEBUG("Inbound ACK: Accept queue overflow triggered (Slot was NULL or ring full)");
                            // Accept Queue Overflow! CC Attack / Ghost connection leak prevention.
                            // By checking queue BEFORE TCB allocation, we prevent botnets from exhausting
                            // the 1-million connection pool with dead connections in the EBR graveyard.
                            
                            // Rate limit RSTs to prevent TX exhaustion self-DDoS
                            if (SL_EXPECT_FALSE(current_time_ms != last_rst_reset_ms_)) {
                                last_rst_reset_ms_ = current_time_ms;
                                rst_tokens_ = 100; // 100k RSTs per second max
                            }
                        if (SL_EXPECT_FALSE(rst_tokens_ == 0)) {
                            SLAB_GW_DEBUG("Inbound ACK: Overflow RST dropped (Rate limit hit)");
                            return;
                        }
                            rst_tokens_--;

                            SLAB_GW_DEBUG("Inbound ACK: Generating stateless RST for queue overflow...");
                            if (SL_EXPECT_TRUE(gateway_egress_ && tx_mbuf_pool_)) {
                                auto* rst_slot = gateway_egress_->get_reserved_slot(0);
                                if (rst_slot) {
                                    rst_slot->mbuf = rte_pktmbuf_alloc(tx_mbuf_pool_);
                                    if (rst_slot->mbuf) {
                                        std::memset(rst_slot->get_header(), 0, 60);
                                        stateless_rst_ack(*rst_slot->get_header(), *hdr);
                                        rst_slot->mbuf->data_len = 60;
                                        rst_slot->mbuf->pkt_len = 60;
                                        rst_slot->set_payload_length(0); // Clean recycled state
                                        gateway_egress_->commit_n(1);
                                    }
                                }
                            }
                            return;
                        }

                        // Valid cookie ACK! Hydrate connection into ESTABLISHED.
                        conn_node = create_connection(tuple);
                        if (SL_EXPECT_FALSE(!conn_node)) {
                            *accept_slot = 0xFFFFFFFF; // Invalid ID placeholder
                            accept_queue_->commit_n(1);
                            GW_DROP("node alloc fail");
                        }

                        tcp_transmission_control_block& tcb = *conn_node->tcb;
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
                        tcb.expected_ttl = hdr->ip_ttl; // Baseline from the hydrating ACK
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

                        // Safely commit the connection to the application layer
                        *accept_slot = conn_node->conn_id;
                        accept_queue_->commit_n(1);
                        return;
                    }
                }
                
                // RFC 793 Connection Rejection
                if (!(hdr->tcp_flags & FLAG_RST)) {
                    // Rate limit invalid connection rejections
                    if (SL_EXPECT_FALSE(current_time_ms != last_rst_reset_ms_)) {
                        last_rst_reset_ms_ = current_time_ms;
                        rst_tokens_ = 100;
                    }
                if (SL_EXPECT_FALSE(rst_tokens_ == 0)) {
                    SLAB_GW_DEBUG("RFC793 RST: Dropped (Rate limit hit)");
                    return;
                }
                    rst_tokens_--;

                    SLAB_GW_DEBUG("RFC793: Dispatching RST for port " << core::endian::network_to_host16(hdr->tcp_dst_port));
                    if (SL_EXPECT_TRUE(gateway_egress_ && tx_mbuf_pool_)) {
                        auto* slot = gateway_egress_->get_reserved_slot(0);
                        if (slot) {
                            slot->mbuf = rte_pktmbuf_alloc(tx_mbuf_pool_);
                            if (slot->mbuf) {
                                std::memset(slot->get_header(), 0, 60);
                                stateless_rst_ack(*slot->get_header(), *hdr);
                                slot->mbuf->data_len = 60;
                                slot->mbuf->pkt_len = 60;
                                slot->set_payload_length(0); // Clean recycled state
                                gateway_egress_->commit_n(1);
                                SLAB_GW_DEBUG("RFC793 RST committed successfully.");
                            } else {
                                SLAB_GW_DEBUG("RFC793 RST: mbuf allocation FAILED.");
                            }
                        }
                    }
                }
                return;
            }

            tcp_transmission_control_block& tcb = *conn_node->tcb; // Now safely inside the function scope

            uint32_t trim_offset = 0;
            if (SL_EXPECT_TRUE(tcp_flow_engine::process_inbound(tcb, *hdr, payload_len, trim_offset, mbuf, header_size, &mbuf_guard.absorbed, conn_node->conn_id))) {
                
                // CRITICAL FIX: Axiom 8 - Allocate OOO matrix for pure FLAG_FIN geometries
                if ((original_payload_len > 0 || (hdr->tcp_flags & FLAG_FIN)) && payload_len == 0 && (tcb.flags_pending & FLAG_ACK) && !tcb.ooo_matrix) {
                    tcb.ooo_matrix = ooo_pool_.make_raw();
                    
                    // OOO Pool Exhausted! Global Memory Pressure Pruning
                    if (SL_EXPECT_FALSE(!tcb.ooo_matrix)) {
                        // Iteratively scan buckets and reclaim OOO matrices from idle connections
                        for (size_t p = 0; p < 256; ++p) {
                            size_t prune_idx = (sweep_cursor_ + p) % NUM_BUCKETS;
                            hash_node* p_curr = buckets_[prune_idx].load(std::memory_order_relaxed);
                            while (p_curr != nullptr) {
                                if (p_curr->tcb->ooo_matrix && p_curr->tcb != &tcb) {
                                    static_cast<tcp_spatial_ooo_matrix*>(p_curr->tcb->ooo_matrix)->reset(0);
                                    ooo_pool_.release(static_cast<tcp_spatial_ooo_matrix*>(p_curr->tcb->ooo_matrix));
                                    p_curr->tcb->ooo_matrix = nullptr;
                                    tcb.ooo_matrix = ooo_pool_.make_raw();
                                    if (tcb.ooo_matrix) break;
                                }
                                p_curr = p_curr->next.load(std::memory_order_relaxed);
                            }
                            if (tcb.ooo_matrix) break;
                        }
                    }

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

                if (payload_len > 0) {
                    inbound_stream_frame app_frame { payload_ptr + trim_offset, payload_len, conn_node->conn_id };
                    defragmenter_.on(app_frame);
                }
                
                // Zero-Copy Reassembly execution
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
                        inbound_stream_frame ooo_frame { ext_data, ext_len, conn_node->conn_id };
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
                tcp_flow_engine::dispatch_control_frames(tcb);
                
                // Clear Karn's Guard if we received a valid ACK that safely advanced the window
                if (hdr->tcp_flags & FLAG_ACK) {
                    uint32_t host_ack = core::endian::network_to_host32(hdr->tcp_ack);
                    if (host_ack == tcb.snd_nxt) {
                        tcb.temporal_flags &= ~TEMP_FLAG_RETRANSMIT;
                    }
                }

                // Reclaim OOO matrix gracefully upon teardown
                if (SL_EXPECT_FALSE(tcb.phase_mask == PHASE_CLOSED || tcb.phase_mask == PHASE_TIME_WAIT)) {
                    if (tcb.ooo_matrix) {
                        static_cast<tcp_spatial_ooo_matrix*>(tcb.ooo_matrix)->reset(0); // Free unconsumed hardware mbufs
                        ooo_pool_.release(static_cast<tcp_spatial_ooo_matrix*>(tcb.ooo_matrix));
                        tcb.ooo_matrix = nullptr;
                    }
                }
            }
        }

        /**
         * @brief Native IPv6 Extractor & Flow Router.
         * @details Structurally isolated from the IPv4 hot-path to preserve L1 bounds.
         */
        SLAB_HOT void on_raw_frame_ipv6(const char* data, size_t length, uint64_t current_time_ms, struct rte_mbuf* mbuf) noexcept {
            mbuf_cleanup_guard mbuf_guard{mbuf};
            
            if (SL_EXPECT_FALSE(length < 74)) GW_DROP("ipv6 short");
            
            const auto* hdr = reinterpret_cast<const raw_tcp_ipv6_frame*>(data);
            
            // CRITICAL FIX: Multicast/Broadcast Source Injection Drop
            // IPv6 multicast addresses always start with 0xFF
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
            
            if (nxt_hdr == 58) { 
                if (logical_len >= l4_offset + 8) {
                    uint8_t icmp6_type = data[l4_offset];
                    uint8_t icmp6_code = data[l4_offset + 1];
                    
                    if (icmp6_type == 2 && icmp6_code == 0) { // Packet Too Big (PMTUD)
                        if (logical_len >= l4_offset + 8 + 40 + 8) {
                            uint32_t next_hop_mtu = core::endian::network_to_host32(*reinterpret_cast<const uint32_t*>(&data[l4_offset + 4]));
                            
                            // Shift pointer backward by 14 bytes so the inner L3 fields align with raw_tcp_ipv6_frame offsets natively
                            const auto* orig_ip6 = reinterpret_cast<const raw_tcp_ipv6_frame*>(&data[l4_offset + 8 - 14]); 
                            
                            if (orig_ip6->ipv6_nxt == 6) {
                                tcp_4tuple tuple{};
                                tuple.src_ipv6[0] = orig_ip6->ipv6_dst[0];
                                tuple.src_ipv6[1] = orig_ip6->ipv6_dst[1];
                                tuple.dst_ipv6[0] = orig_ip6->ipv6_src[0];
                                tuple.dst_ipv6[1] = orig_ip6->ipv6_src[1];
                                tuple.src_port = orig_ip6->tcp_dst_port; 
                                tuple.dst_port = orig_ip6->tcp_src_port; 
                                tuple.is_ipv6 = 1;

                                hash_node* conn_node = lookup_node(tuple);
                                if (conn_node) {
                                    uint32_t inner_seq = core::endian::network_to_host32(orig_ip6->tcp_seq);
                                    tcp_transmission_control_block& tcb = *conn_node->tcb;
                                    uint32_t inflight = tcb.snd_nxt - tcb.snd_una;
                                    if (inner_seq - tcb.snd_una <= inflight) {
                                        uint16_t new_mss = (next_hop_mtu > 60) ? static_cast<uint16_t>(next_hop_mtu - 60) : 1220;
                                        if (new_mss < tcb.remote_mss) tcb.remote_mss = new_mss;
                                    }
                                }
                            }
                        }
                    } else if (icmp6_type == 128 && icmp6_code == 0) {
                        uint32_t icmp_len = logical_len - l4_offset;
                        if (SL_EXPECT_FALSE(logical_len > 1514)) GW_DROP("icmp6 oversized");
                        if (SL_EXPECT_TRUE(gateway_egress_ && tx_mbuf_pool_)) {
                            auto* slot = gateway_egress_->get_reserved_slot(0);
                            if (slot) {
                                slot->mbuf = rte_pktmbuf_alloc(tx_mbuf_pool_);
                                if (slot->mbuf) {
                                    char* out_data = rte_pktmbuf_mtod(slot->mbuf, char*);
                                    uint32_t reply_len = 54 + icmp_len;
                                    std::memcpy(out_data, data, 54);
                                    std::memcpy(out_data + 54, data + l4_offset, icmp_len);
                                    auto* out_hdr = reinterpret_cast<raw_tcp_ipv6_frame*>(out_data);
                                    out_hdr->ipv6_nxt = 58;
                                    out_hdr->ipv6_plen = core::endian::host_to_network16(icmp_len);
                                    __builtin_memcpy(out_hdr->dest_mac, hdr->src_mac, 6);
                                    __builtin_memcpy(out_hdr->src_mac, local_mac_, 6);
                                    out_hdr->ipv6_dst[0] = hdr->ipv6_src[0];
                                    out_hdr->ipv6_dst[1] = hdr->ipv6_src[1];
                                    out_hdr->ipv6_src[0] = local_ipv6_[0];
                                    out_hdr->ipv6_src[1] = local_ipv6_[1];
                                    
                                    out_data[54] = 129;
                                    *reinterpret_cast<uint16_t*>(&out_data[56]) = 0;
                                    
                                    uint32_t pseudo_sum = 0;
                                    for(int i=0; i<8; ++i) pseudo_sum += reinterpret_cast<const uint16_t*>(out_hdr->ipv6_src)[i];
                                    for(int i=0; i<8; ++i) pseudo_sum += reinterpret_cast<const uint16_t*>(out_hdr->ipv6_dst)[i];
                                    pseudo_sum += core::endian::host_to_network16(58);
                                    pseudo_sum += core::endian::host_to_network16(icmp_len);
                                    *reinterpret_cast<uint16_t*>(&out_data[56]) = tcp_wire_engine::compute_checksum(&out_data[54], icmp_len, pseudo_sum);
                                    
                                    slot->mbuf->data_len = reply_len < 60 ? 60 : reply_len;
                                    slot->mbuf->pkt_len = slot->mbuf->data_len;
                                    gateway_egress_->commit_n(1);
                                }
                            }
                        }
                    } else if (icmp6_type == 130) { // Multicast Listener Query
                        if (logical_len >= l4_offset + 24) {
                            uint64_t snm[2];
                            uint8_t* snm_bytes = reinterpret_cast<uint8_t*>(snm);
                            std::memset(snm_bytes, 0, 16);
                            snm_bytes[0] = 0xFF; snm_bytes[1] = 0x02;
                            snm_bytes[11] = 0x01; snm_bytes[12] = 0xFF;
                            const uint8_t* loc_bytes = reinterpret_cast<const uint8_t*>(&local_ipv6_[1]);
                            snm_bytes[13] = loc_bytes[5];
                            snm_bytes[14] = loc_bytes[6];
                            snm_bytes[15] = loc_bytes[7];
                            // join_multicast_group_ipv6(snm); // TODO: implement MLD natively
                        } // MLD logic will be implemented here
                    } else if (icmp6_type == 135) { // Neighbor Solicitation
                        if (logical_len >= l4_offset + 24) {
                            const uint64_t* target = reinterpret_cast<const uint64_t*>(&data[l4_offset + 8]);
                            if (target[0] == local_ipv6_[0] && target[1] == local_ipv6_[1]) {
                                if (SL_EXPECT_TRUE(gateway_egress_ && tx_mbuf_pool_)) {
                                    auto* slot = gateway_egress_->get_reserved_slot(0);
                                    if (slot && (slot->mbuf = rte_pktmbuf_alloc(tx_mbuf_pool_))) {
                                        char* out_data = rte_pktmbuf_mtod(slot->mbuf, char*);
                                        std::memset(out_data, 0, 86);
                                        __builtin_memcpy(out_data, hdr->src_mac, 6);
                                        __builtin_memcpy(out_data + 6, local_mac_, 6);
                                        *reinterpret_cast<uint16_t*>(out_data + 12) = core::endian::host_to_network16(0x86DD);
                                        *reinterpret_cast<uint32_t*>(out_data + 14) = core::endian::host_to_network32(0x60000000);
                                        *reinterpret_cast<uint16_t*>(out_data + 18) = core::endian::host_to_network16(32);
                                        out_data[20] = 58; out_data[21] = 255;
                                        __builtin_memcpy(out_data + 22, local_ipv6_, 16);
                                        __builtin_memcpy(out_data + 38, hdr->ipv6_src, 16);
                                        out_data[54] = 136;
                                        out_data[58] = 0x60;
                                        __builtin_memcpy(out_data + 62, local_ipv6_, 16);
                                        out_data[78] = 2; out_data[79] = 1;
                                        __builtin_memcpy(out_data + 80, local_mac_, 6);
                                        
                                        uint32_t pseudo_sum = 0;
                                        for(int i=0; i<8; ++i) pseudo_sum += reinterpret_cast<const uint16_t*>(out_data + 22)[i];
                                        for(int i=0; i<8; ++i) pseudo_sum += reinterpret_cast<const uint16_t*>(out_data + 38)[i];
                                        pseudo_sum += core::endian::host_to_network16(58);
                                        pseudo_sum += core::endian::host_to_network16(32);
                                        *reinterpret_cast<uint16_t*>(out_data + 56) = tcp_wire_engine::compute_checksum(out_data + 54, 32, pseudo_sum);
                                        
                                        slot->mbuf->data_len = 86; slot->mbuf->pkt_len = 86;
                                        gateway_egress_->commit_n(1);
                                    }
                                }
                            }
                        }
                    } else if (icmp6_type == 136) { // Neighbor Advertisement
                        if (logical_len >= l4_offset + 24) {
                            const uint64_t* target = reinterpret_cast<const uint64_t*>(&data[l4_offset + 8]);
                            if (logical_len >= l4_offset + 32 && data[l4_offset + 24] == 2 && data[l4_offset + 25] == 1) {
                                ndp_cache_.insert(target, reinterpret_cast<const uint8_t*>(&data[l4_offset + 26]), current_time_ms);
                            } else {
                                ndp_cache_.insert(target, hdr->src_mac, current_time_ms);
                            }
                        }
                    }
                }
                return;
            }
            if (SL_EXPECT_FALSE(nxt_hdr != 6)) GW_DROP("not tcp v6");
            
            // Check for No Next Header (59) which means packet is dropped
            if (SL_EXPECT_FALSE(nxt_hdr == 59)) GW_DROP("ipv6 no next hdr");

            // Re-evaluate current L4 offset after processing all extension headers.
            // If still not TCP, drop.
            if (SL_EXPECT_FALSE(nxt_hdr != 6)) GW_DROP("ipv6 unsupported L4 after ext hdrs");

            // If TCP, ensure the L4 offset is sane after all extension headers.
            if (SL_EXPECT_FALSE(l4_offset > (14 + 40 + 64))) GW_DROP("ipv6 tcp hdr too far");
            
            // Construct 128-bit IPv6 Tuple for AES Hashing
            tcp_4tuple tuple{};
            tuple.src_ipv6[0] = hdr->ipv6_src[0];
            tuple.src_ipv6[1] = hdr->ipv6_src[1];
            tuple.dst_ipv6[0] = hdr->ipv6_dst[0];
            tuple.dst_ipv6[1] = hdr->ipv6_dst[1];
            tuple.src_port = hdr->tcp_src_port;
            tuple.dst_port = hdr->tcp_dst_port;
            tuple.is_ipv6 = 1;
            
            uint8_t data_offset_words = hdr->tcp_data_offset >> 4;
            if (SL_EXPECT_FALSE(data_offset_words < 5)) GW_DROP("tcp6 offset < 5");
            uint32_t header_size = 14 + 40 + (data_offset_words * 4);
            if (SL_EXPECT_FALSE(logical_len < header_size)) GW_DROP("tcp6 offset bounds");

            uint32_t payload_len = logical_len - header_size;
            uint32_t original_payload_len = payload_len;
            const char* payload_ptr = data + header_size;

            if (SL_EXPECT_FALSE((hdr->tcp_flags & (FLAG_SYN | FLAG_FIN)) == (FLAG_SYN | FLAG_FIN))) GW_DROP("tcp6 syn-fin");
            if (SL_EXPECT_FALSE(!(hdr->tcp_flags & (FLAG_SYN | FLAG_ACK | FLAG_RST)))) GW_DROP("tcp6 bad flags");

            uint16_t tcp_len = logical_len - 54;
            uint32_t pseudo_sum = 0;
            const uint16_t* src16 = reinterpret_cast<const uint16_t*>(hdr->ipv6_src);
            const uint16_t* dst16 = reinterpret_cast<const uint16_t*>(hdr->ipv6_dst);
            for(int i=0; i<8; ++i) pseudo_sum += src16[i];
            for(int i=0; i<8; ++i) pseudo_sum += dst16[i];
            pseudo_sum += core::endian::host_to_network16(6);
            pseudo_sum += core::endian::host_to_network16(tcp_len);
            
            uint16_t tcp_csum = tcp_wire_engine::compute_checksum(reinterpret_cast<const char*>(hdr) + 54, tcp_len, pseudo_sum);
            if (SL_EXPECT_FALSE(tcp_csum != 0 && tcp_csum != 0xFFFF)) GW_DROP("tcp6 csum fail");

            hash_node* conn_node = lookup_node(tuple);
            
            if (!conn_node) {
                if ((hdr->tcp_flags & (FLAG_SYN | FLAG_ACK | FLAG_RST | FLAG_FIN)) == FLAG_SYN) {
                    if (SL_EXPECT_FALSE(hdr->src_mac[0] & 1)) GW_DROP("syn bcast mac v6");
                    if (SL_EXPECT_FALSE(payload_len > 0)) GW_DROP("syn payload v6");

                    uint16_t client_mss = 1220; // Lower baseline for IPv6
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
                            if (kind == 2 && len == 4) { 
                                client_mss = (opt_ptr[2] << 8) | opt_ptr[3]; 
                                if (SL_EXPECT_FALSE(client_mss < 1220)) client_mss = 1220;
                            }
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
                        conn_node = lookup_node(tuple);
                        if (SL_EXPECT_FALSE(conn_node)) GW_DROP("dup conn node v6");

                        if (SL_EXPECT_FALSE(!accept_queue_)) GW_DROP("no accept queue");
                        uint32_t* accept_slot = accept_queue_->get_reserved_slot(0);
                        if (SL_EXPECT_FALSE(!accept_slot || accept_queue_->available_to_peek() >= 1023)) { 
                            if (SL_EXPECT_FALSE(current_time_ms != last_rst_reset_ms_)) {
                                last_rst_reset_ms_ = current_time_ms;
                                rst_tokens_ = 100;
                            }
                            if (SL_EXPECT_FALSE(rst_tokens_ == 0)) return;
                            rst_tokens_--;

                            if (SL_EXPECT_TRUE(gateway_egress_ && tx_mbuf_pool_)) {
                                auto* rst_slot = gateway_egress_->get_reserved_slot(0);
                                if (rst_slot) {
                                    rst_slot->mbuf = rte_pktmbuf_alloc(tx_mbuf_pool_);
                                    if (rst_slot->mbuf) {
                                        std::memset(rst_slot->get_header(), 0, 80);
                                        stateless_rst_ack_ipv6(*reinterpret_cast<raw_tcp_ipv6_frame*>(rst_slot->get_header()), *hdr);
                                        rst_slot->mbuf->data_len = 74;
                                        rst_slot->mbuf->pkt_len = 74;
                                        rst_slot->set_payload_length(0); 
                                        gateway_egress_->commit_n(1);
                                    }
                                }
                            }
                            return;
                        }

                        conn_node = create_connection(tuple);
                        if (SL_EXPECT_FALSE(!conn_node)) {
                            *accept_slot = 0xFFFFFFFF;
                            accept_queue_->commit_n(1);
                            GW_DROP("node alloc fail v6");
                        }

                        tcp_transmission_control_block& tcb = *conn_node->tcb;
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
                        tcb.rcv_wnd = 65535;
                        tcb.snd_wnd = static_cast<uint32_t>(core::endian::network_to_host16(hdr->tcp_window)) << rec_wscale;
                        tcb.cwnd = 1460u * 10;
                        tcb.ssthresh = 65535u;

                        *accept_slot = conn_node->conn_id;
                        accept_queue_->commit_n(1);
                        return;
                    }
                }
                
                if (!(hdr->tcp_flags & FLAG_RST)) {
                    if (SL_EXPECT_FALSE(current_time_ms != last_rst_reset_ms_)) {
                        last_rst_reset_ms_ = current_time_ms;
                        rst_tokens_ = 100;
                    }
                    if (SL_EXPECT_FALSE(rst_tokens_ == 0)) return;
                    rst_tokens_--;

                    if (SL_EXPECT_TRUE(gateway_egress_ && tx_mbuf_pool_)) {
                        auto* slot = gateway_egress_->get_reserved_slot(0);
                        if (slot) {
                            slot->mbuf = rte_pktmbuf_alloc(tx_mbuf_pool_);
                            if (slot->mbuf) {
                                std::memset(slot->get_header(), 0, 80);
                                stateless_rst_ack_ipv6(*reinterpret_cast<raw_tcp_ipv6_frame*>(slot->get_header()), *hdr);
                                slot->mbuf->data_len = 74;
                                slot->mbuf->pkt_len = 74;
                                slot->set_payload_length(0); 
                                gateway_egress_->commit_n(1);
                            }
                        }
                    }
                }
                return;
            }

            tcp_transmission_control_block& tcb = *conn_node->tcb;
            uint32_t trim_offset = 0;
            if (SL_EXPECT_TRUE(tcp_flow_engine::process_inbound(tcb, *hdr, payload_len, trim_offset, mbuf, header_size, &mbuf_guard.absorbed, conn_node->conn_id))) {
                if ((original_payload_len > 0 || (hdr->tcp_flags & FLAG_FIN)) && payload_len == 0 && (tcb.flags_pending & FLAG_ACK) && !tcb.ooo_matrix) {
                    tcb.ooo_matrix = ooo_pool_.make_raw();
                    
                    if (SL_EXPECT_FALSE(!tcb.ooo_matrix)) {
                        for (size_t p = 0; p < 256; ++p) {
                            size_t prune_idx = (sweep_cursor_ + p) % NUM_BUCKETS;
                            hash_node* p_curr = buckets_[prune_idx].load(std::memory_order_relaxed);
                            while (p_curr != nullptr) {
                                if (p_curr->tcb->ooo_matrix && p_curr->tcb != &tcb) {
                                    static_cast<tcp_spatial_ooo_matrix*>(p_curr->tcb->ooo_matrix)->reset(0);
                                    ooo_pool_.release(static_cast<tcp_spatial_ooo_matrix*>(p_curr->tcb->ooo_matrix));
                                    p_curr->tcb->ooo_matrix = nullptr;
                                    tcb.ooo_matrix = ooo_pool_.make_raw();
                                    if (tcb.ooo_matrix) break;
                                }
                                p_curr = p_curr->next.load(std::memory_order_relaxed);
                            }
                            if (tcb.ooo_matrix) break;
                        }
                    }

                    if (tcb.ooo_matrix) {
                        static_cast<tcp_spatial_ooo_matrix*>(tcb.ooo_matrix)->reset(tcb.rcv_nxt);
                        uint32_t host_seq = core::endian::network_to_host32(hdr->tcp_seq);
                        uint32_t payload_seq = host_seq + ((hdr->tcp_flags & FLAG_SYN) ? 1 : 0);
                        if (payload_seq - tcb.rcv_nxt <= 0x7FFFFFFF && mbuf) {
                            mbuf_guard.absorbed = static_cast<tcp_spatial_ooo_matrix*>(tcb.ooo_matrix)->insert_mbuf(tcb.rcv_nxt, payload_seq, mbuf, header_size, original_payload_len, hdr->tcp_flags);
                        }
                    }
                }

                if (payload_len > 0) {
                    inbound_stream_frame app_frame { payload_ptr + trim_offset, payload_len, conn_node->conn_id };
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
                        inbound_stream_frame ooo_frame { ext_data, ext_len, conn_node->conn_id };
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
                tcp_flow_engine::dispatch_control_frames(tcb);
                
                if (hdr->tcp_flags & FLAG_ACK) {
                    uint32_t host_ack = core::endian::network_to_host32(hdr->tcp_ack);
                    if (host_ack == tcb.snd_nxt) {
                        tcb.temporal_flags &= ~TEMP_FLAG_RETRANSMIT;
                    }
                }

                if (SL_EXPECT_FALSE(tcb.phase_mask == PHASE_CLOSED || tcb.phase_mask == PHASE_TIME_WAIT)) {
                    if (tcb.ooo_matrix) {
                        static_cast<tcp_spatial_ooo_matrix*>(tcb.ooo_matrix)->reset(0);
                        ooo_pool_.release(static_cast<tcp_spatial_ooo_matrix*>(tcb.ooo_matrix));
                        tcb.ooo_matrix = nullptr;
                    }
                }
            }
        }

        /**
         * @brief Amortized Temporal Sweep.
         * @details Scans a bounded slice of the 1M connection buckets per tick.
         * Handles Retransmission (RTO), Keep-Alives, and safe eviction of dead connections.
         */
        SLAB_HOT void poll_temporal(uint64_t current_time_ms) noexcept {
            uint64_t current_time_tsc = current_time_ms * 3000000ULL;
            // Sweep exactly 1024 buckets per tick to avoid L1-cache displacement and latency jitter
            constexpr size_t SWEEP_BATCH = 1024;
            
            for (size_t i = 0; i < SWEEP_BATCH; ++i) {
                size_t bucket_idx = sweep_cursor_;
                sweep_cursor_ = (sweep_cursor_ + 1) % NUM_BUCKETS;

                hash_node* prev = nullptr;
                hash_node* curr = buckets_[bucket_idx].load(std::memory_order_acquire);
                
                while (curr != nullptr) {
                    tcp_transmission_control_block& tcb = *curr->tcb;
                    hash_node* next = curr->next.load(std::memory_order_acquire);

                    if (SL_EXPECT_TRUE(tcb.phase_mask != PHASE_CLOSED && tcb.phase_mask != PHASE_LISTEN)) {
                        
                        // Active ARP Resolution Strategy for Outbound Connections
                        if (SL_EXPECT_FALSE(!tcb.is_ipv6 && (tcb.temporal_flags & TEMP_FLAG_ARP_WAIT))) {
                            // FIB Routing: Resolve Gateway MAC if Target IP is outside local subnet
                            uint32_t next_hop_ip = (tcb.remote_ipv4 & subnet_mask_) == (local_ipv4_ & subnet_mask_) 
                                                 ? tcb.remote_ipv4 : default_gateway_ipv4_;

                            if (arp_cache_.resolve(next_hop_ip, tcb.target_mac)) {
                                tcb.temporal_flags &= ~TEMP_FLAG_ARP_WAIT;
                                tcb.rto_deadline_tsc = 0; // Clear timeout
                                tcb.flags_pending |= FLAG_SYN; // Resume connection ignition
                            } else if (current_time_ms >= tcb.persist_deadline_tsc) {
                                if (SL_EXPECT_TRUE(gateway_egress_ && tx_mbuf_pool_)) {
                                    auto* slot = gateway_egress_->get_reserved_slot(0);
                                    if (slot) {
                                        slot->mbuf = rte_pktmbuf_alloc(tx_mbuf_pool_);
                                        if (slot->mbuf) {
                                            auto* out_arp = reinterpret_cast<raw_arp_frame*>(rte_pktmbuf_mtod(slot->mbuf, void*));
                                            std::memset(out_arp, 0, sizeof(raw_arp_frame));
                                            std::memset(out_arp->dest_mac, 0xFF, 6); // Global Broadcast
                                            __builtin_memcpy(out_arp->src_mac, local_mac_, 6);
                                            out_arp->eth_type = core::endian::host_to_network16(0x0806);
                                            out_arp->hw_type = core::endian::host_to_network16(1);
                                            out_arp->proto_type = core::endian::host_to_network16(0x0800);
                                            out_arp->hw_len = 6;
                                            out_arp->proto_len = 4;
                                            out_arp->opcode = core::endian::host_to_network16(1); // Request
                                            __builtin_memcpy(out_arp->sender_mac, local_mac_, 6);
                                            out_arp->sender_ip = local_ipv4_;
                                            std::memset(out_arp->target_mac, 0x00, 6);
                                            out_arp->target_ip = next_hop_ip;
                                            
                                            // The raw_arp_frame struct is 64 bytes and already zero-initialized.
                                            slot->mbuf->data_len = 64;
                                            slot->mbuf->pkt_len = 64;
                                            gateway_egress_->commit_n(1);
                                        }
                                    }
                                }
                                tcb.persist_deadline_tsc = current_time_ms + 1000; // Broadcast backoff
                            }
                        } else if (SL_EXPECT_FALSE(tcb.is_ipv6 && (tcb.temporal_flags & TEMP_FLAG_ARP_WAIT))) {
                            if (ndp_cache_.resolve(tcb.remote_ipv6, tcb.target_mac)) {
                                tcb.temporal_flags &= ~TEMP_FLAG_ARP_WAIT;
                                tcb.rto_deadline_tsc = 0;
                                tcb.flags_pending |= FLAG_SYN;
                            } else if (current_time_ms >= tcb.persist_deadline_tsc) {
                                if (SL_EXPECT_TRUE(gateway_egress_ && tx_mbuf_pool_)) {
                                    auto* slot = gateway_egress_->get_reserved_slot(0);
                                    if (slot && (slot->mbuf = rte_pktmbuf_alloc(tx_mbuf_pool_))) {
                                        char* out_data = rte_pktmbuf_mtod(slot->mbuf, char*);
                                        std::memset(out_data, 0, 86);
                                        out_data[0] = 0x33; out_data[1] = 0x33; out_data[2] = 0xFF;
                                        out_data[3] = (reinterpret_cast<const uint8_t*>(&tcb.remote_ipv6[1]))[5];
                                        out_data[4] = (reinterpret_cast<const uint8_t*>(&tcb.remote_ipv6[1]))[6];
                                        out_data[5] = (reinterpret_cast<const uint8_t*>(&tcb.remote_ipv6[1]))[7];
                                        __builtin_memcpy(out_data + 6, local_mac_, 6);
                                        *reinterpret_cast<uint16_t*>(out_data + 12) = core::endian::host_to_network16(0x86DD);
                                        *reinterpret_cast<uint32_t*>(out_data + 14) = core::endian::host_to_network32(0x60000000);
                                        *reinterpret_cast<uint16_t*>(out_data + 18) = core::endian::host_to_network16(32);
                                        out_data[20] = 58; out_data[21] = 255;
                                        __builtin_memcpy(out_data + 22, local_ipv6_, 16);
                                        uint8_t snm[16] = {0xFF, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0xFF, static_cast<uint8_t>(out_data[3]), static_cast<uint8_t>(out_data[4]), static_cast<uint8_t>(out_data[5])};
                                        __builtin_memcpy(out_data + 38, snm, 16);
                                        out_data[54] = 135; out_data[55] = 0;
                                        __builtin_memcpy(out_data + 62, tcb.remote_ipv6, 16);
                                        out_data[78] = 1; out_data[79] = 1;
                                        __builtin_memcpy(out_data + 80, local_mac_, 6);
                                        uint32_t pseudo_sum = 0;
                                        for(int k=0; k<8; ++k) pseudo_sum += reinterpret_cast<const uint16_t*>(local_ipv6_)[k];
                                        for(int k=0; k<8; ++k) pseudo_sum += reinterpret_cast<const uint16_t*>(snm)[k];
                                        pseudo_sum += core::endian::host_to_network16(58);
                                        pseudo_sum += core::endian::host_to_network16(32);
                                        *reinterpret_cast<uint16_t*>(out_data + 56) = tcp_wire_engine::compute_checksum(out_data + 54, 32, pseudo_sum);
                                        slot->mbuf->data_len = 86; slot->mbuf->pkt_len = 86;
                                        gateway_egress_->commit_n(1);
                                    }
                                }
                                tcb.persist_deadline_tsc = current_time_ms + 1000;
                            }
                        }

                        uint32_t prior_snd_nxt = tcb.snd_nxt;

                        tcp_temporal_wheel::on_tick(current_time_ms, tcb, reinterpret_cast<rto_tracker&>(tcb.srtt));

                        // Active Retransmission Replay (Restored for Public Edge)
                        if (SL_EXPECT_FALSE(static_cast<int32_t>(tcb.snd_nxt - prior_snd_nxt) < 0)) {
                            if (SL_EXPECT_FALSE(tcb.snd_wnd == 0)) {
                                tcb.temporal_flags |= TEMP_FLAG_ZWP_ACTIVE;
                                tcb.snd_nxt = prior_snd_nxt;
                            } else {
                                if (tcb.tx_unacked_ring && tcb.tx_egress_conduit) {
                                    auto* unacked = static_cast<core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024>*>(tcb.tx_unacked_ring);
                                    auto* egress = static_cast<core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024>*>(tcb.tx_egress_conduit);
                                    
                                    size_t available = unacked->available_to_peek();
                                    uint32_t usable_window = std::min(tcb.cwnd, tcb.snd_wnd);
                                    
                                    for (size_t j = 0; j < available; ++j) {
                                        const outbound_tcp_segment<1460>* lost_frame = unacked->get_peek_slot(j);
                                        uint32_t frame_len = lost_frame->get_payload_length() + ((lost_frame->get_header()->tcp_flags & (FLAG_SYN | FLAG_FIN)) ? 1 : 0);
                                        if (tcb.snd_nxt - tcb.snd_una + frame_len > usable_window) break;
                                        
                                        outbound_tcp_segment<1460>* slot = egress->get_reserved_slot(0);
                                        if (slot) {
                                            slot->mbuf = lost_frame->mbuf;
                                            if (SL_EXPECT_FALSE(!slot->retain())) {
                                                tcb.phase_mask = PHASE_CLOSED;
                                                tcb.flags_pending |= FLAG_RST;
                                                break;
                                            }
                                            slot->get_header()->tcp_ack = core::endian::host_to_network32(tcb.rcv_nxt);
                                            uint32_t wnd = tcb.rcv_wnd >> tcb.rcv_wscale;
                                            slot->get_header()->tcp_window = core::endian::host_to_network16(wnd > 65535 ? 65535 : wnd);
                                            
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
                                            tcb.snd_nxt += frame_len;
                                        } else {
                                            break;
                                        }
                                    }
                                }
                            }
                        }

                        tcp_flow_engine::dispatch_control_frames(tcb);

                        // Dispatch temporal state to L7 (TLS/App) natively if supported
                        if constexpr (requires { defragmenter_.on_temporal(curr->conn_id, current_time_ms, tcb); }) {
                            defragmenter_.on_temporal(curr->conn_id, current_time_ms, tcb);
                        }

                        // VULNERABILITY FIX: Prevent OOO Memory Leak on Temporal Expiration
                        if (SL_EXPECT_FALSE(tcb.phase_mask == PHASE_CLOSED || tcb.phase_mask == PHASE_TIME_WAIT)) {
                            if (tcb.ooo_matrix) {
                                static_cast<tcp_spatial_ooo_matrix*>(tcb.ooo_matrix)->reset(0);
                                ooo_pool_.release(static_cast<tcp_spatial_ooo_matrix*>(tcb.ooo_matrix));
                                tcb.ooo_matrix = nullptr;
                            }

                            // CRITICAL FIX: Axiom 1 - Topological Orphan Isolation in MBUF Substrate Rings
                            if (SL_EXPECT_FALSE(tcb.tx_unacked_ring)) {
                                auto* unacked = static_cast<core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024>*>(tcb.tx_unacked_ring);
                                while (unacked->available_to_peek() > 0) {
                                    const_cast<outbound_tcp_segment<1460>*>(unacked->get_peek_slot(0))->release();
                                    unacked->consume_n(1);
                                }
                            }
                        }
                        
                        prev = curr;
                    } else if (tcb.phase_mask == PHASE_CLOSED) {
                        // VULNERABILITY FIX: Prevent OOO Leak on L7 Active Close
                        if (SL_EXPECT_FALSE(tcb.ooo_matrix)) {
                            static_cast<tcp_spatial_ooo_matrix*>(tcb.ooo_matrix)->reset(0);
                            ooo_pool_.release(static_cast<tcp_spatial_ooo_matrix*>(tcb.ooo_matrix));
                            tcb.ooo_matrix = nullptr;
                        }

                        // CRITICAL FIX: Axiom 1 - Topological Orphan Isolation in MBUF Substrate Rings
                        if (SL_EXPECT_FALSE(tcb.tx_unacked_ring)) {
                            auto* unacked = static_cast<core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024>*>(tcb.tx_unacked_ring);
                            while (unacked->available_to_peek() > 0) {
                                const_cast<outbound_tcp_segment<1460>*>(unacked->get_peek_slot(0))->release();
                                unacked->consume_n(1);
                            }
                        }

                        // Lock-free eviction logic
                        if (prev == nullptr) {
                            // Attempt to detach head
                            if (buckets_[bucket_idx].compare_exchange_strong(curr, next, std::memory_order_release, std::memory_order_relaxed)) {
                                ebr_graveyard_.retire(curr, epoch_mgr_.current_epoch());
                                curr = next;
                                continue; // Avoid advancing 'prev'
                            }
                        } else {
                            // Attempt to detach internal node
                            if (prev->next.compare_exchange_strong(curr, next, std::memory_order_release, std::memory_order_relaxed)) {
                                ebr_graveyard_.retire(curr, epoch_mgr_.current_epoch());
                                curr = next;
                                continue;
                            }
                        }
                        prev = curr;
                    }
                    curr = next;
                }
            }

            // Hazard EBR Reclamation Sweep
            epoch_mgr_.advance();
            ebr_graveyard_.scavenge(epoch_mgr_.get_safe_epoch(), pool_wrapper_);
        }
    };
}