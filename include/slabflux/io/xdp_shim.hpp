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
#include <bpf/xsk.h>
#include <bpf/bpf.h> // For bpf_map_update_elem
#include <linux/if_xdp.h>
#include <cstdint>

namespace slabflux::io::xdp_shim {

    struct umem_params {
        uint32_t fill_size;
        uint32_t comp_size;
        uint32_t frame_size;
        uint32_t headroom;
        uint32_t flags;
    };

    struct socket_params {
        uint32_t rx_size;
        uint32_t tx_size;
        uint32_t xdp_flags;
        uint32_t bind_flags;
    };

    /**
     * @brief UMEM resource creation wrapper.
     */
    inline int umem_create(xsk_umem** umem, void* addr, uint64_t size, 
                                    xsk_ring_prod* fill, xsk_ring_cons* comp, 
                                    const umem_params& p) noexcept {
        struct xsk_umem_config cfg = {
            .fill_size = p.fill_size,
            .comp_size = p.comp_size,
            .frame_size = p.frame_size,
            .frame_headroom = p.headroom,
            .flags = p.flags
        };
        return ::xsk_umem__create(umem, addr, size, fill, comp, &cfg);
    }

    /**
     * @brief XSK Socket Injection.
     */
    inline int socket_create(xsk_socket** xsk, const char* ifname, uint32_t queue_id, 
                                      xsk_umem* umem, xsk_ring_cons* rx, xsk_ring_prod* tx, 
                                      const socket_params& p) noexcept {
        struct xsk_socket_config cfg = {
            .rx_size = p.rx_size,
            .tx_size = p.tx_size,
            .libbpf_flags = 0,
            .xdp_flags = p.xdp_flags,
            .bind_flags = static_cast<__u16>(p.bind_flags)
        };
        return ::xsk_socket__create(xsk, ifname, queue_id, umem, rx, tx, &cfg);
    }

    inline void socket_delete(xsk_socket* xsk) noexcept {
        ::xsk_socket__delete(xsk);
    }

    inline void umem_delete(xsk_umem* umem) noexcept {
        ::xsk_umem__delete(umem);
    }

    // Ring Access Wrappers
    inline uint32_t fill_reserve(xsk_ring_prod* r, uint32_t n, uint32_t* idx) noexcept {
        return xsk_ring_prod__reserve(r, n, idx);
    }

    inline __u64* fill_addr(xsk_ring_prod* r, uint32_t idx) noexcept {
        return xsk_ring_prod__fill_addr(r, idx);
    }

    inline const __u64* comp_addr(xsk_ring_cons* r, uint32_t idx) noexcept {
        return xsk_ring_cons__comp_addr(r, idx);
    }

    inline xdp_desc* tx_desc(xsk_ring_prod* r, uint32_t idx) noexcept { // Fix: Use xsk_ring_prod__tx_desc
        return xsk_ring_prod__tx_desc(r, idx);
    }

    inline void socket_kick(xsk_socket* xsk) noexcept {
        ::sendto(xsk_socket__fd(xsk), NULL, 0, MSG_DONTWAIT, NULL, 0);
    }

    inline void fill_submit(xsk_ring_prod* r, uint32_t n) noexcept {
        xsk_ring_prod__submit(r, n);
    }

    inline int fill_needs_wakeup(xsk_ring_prod* r) noexcept {
        return xsk_ring_prod__needs_wakeup(r);
    }

    inline int socket_fd(xsk_socket* xsk) noexcept {
        return xsk_socket__fd(xsk);
    }

    inline uint32_t rx_peek(xsk_ring_cons* r, uint32_t n, uint32_t* idx) noexcept {
        return xsk_ring_cons__peek(r, n, idx);
    }

    inline const xdp_desc* rx_desc(xsk_ring_cons* r, uint32_t idx) noexcept {
        return xsk_ring_cons__rx_desc(r, idx);
    }

    inline void rx_release(xsk_ring_cons* r, uint32_t n) noexcept {
        xsk_ring_cons__release(r, n);
    }

    /**
     * @brief Map Coordination.
     * @details Provides a specialized interface for BPF map element updates.
     */
    inline int map_update(int map_fd, uint32_t key, int sock_fd) noexcept {
        uint32_t k_u32 = key; // Key is uint32_t for XDP maps
        uint32_t v_u32 = static_cast<uint32_t>(sock_fd); // Value is uint32_t for XDP maps
        return ::bpf_map_update_elem(map_fd, &k_u32, &v_u32, BPF_ANY);
    }
}
