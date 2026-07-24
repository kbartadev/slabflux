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

#include <benchmark/benchmark.h>
#include <memory>
#include <cstring>
#include <atomic>
#include <string_view>

#include "slabflux/core.hpp"
#include "slabflux/transport/http_frame.hpp"
#include "slabflux/transport/baremetal_parser.hpp"
#include "slabflux/net/network_conduit.hpp"
#include "slabflux/platform/os.hpp"

using namespace slabflux;

struct routed_http_event : transport::http_frame {
    static constexpr uint32_t TYPE_ID = 200;
};

struct compute_sink {
    uint64_t verified_count = 0;
    SLAB_FORCE_INLINE void on(routed_http_event& ev) noexcept {
        if (ev.uri.size() > 1 && ev.uri[1] == 'p') {
            verified_count++;
        }
    }
};

/**
 * @brief Kernel Conduit Factory.
 * Establishes a loopback interface for kernel-tax measurement.
 */
struct kernel_conduit_factory {
    static void create_loopback_pair(slabflux::os::socket_t& tx, slabflux::os::socket_t& rx) {
        using namespace slabflux::os;
        
        socket_t listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
        
        bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        listen(listener, 1);

        socklen_t len = sizeof(addr);
        getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &len);

        tx = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        connect(tx, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        rx = accept(listener, nullptr, nullptr);

        set_nonblocking(tx);
        set_nonblocking(rx);
        close_socket(listener);
    }
};

static void BM_NetworkConduit_KernelTax(benchmark::State& state) {
    auto domain = std::make_unique<runtime_domain<routed_http_event>>();
    auto& pool = domain->get_pool<routed_http_event>();
    compute_sink core_logic;

    slabflux::os::socket_t tx_fd, rx_fd;
    kernel_conduit_factory::create_loopback_pair(tx_fd, rx_fd);

    auto tx_bridge = std::make_unique<slabflux::net::network_conduit<routed_http_event, 4096>>();
    auto rx_bridge = std::make_unique<slabflux::net::network_conduit<routed_http_event, 4096>>();
    tx_bridge->bind_socket(tx_fd);
    rx_bridge->bind_socket(rx_fd);

    const char* raw_req = "GET /price HTTP/1.1\r\nHost: slabflux.hft\r\n\r\n";
    std::string_view req_view(raw_req);

    for (auto _ : state) {
        auto ev = pool.make();
        if (ev) {
            ev->reset();
            if (transport::baremetal_parser::parse(req_view, *ev) == transport::parser_status::OK) {
                // Use blocking wait for the benchmark to ensure zero drop during tax measurement
                while (SL_EXPECT_FALSE(!tx_bridge->push(ev))) { _mm_pause(); }
            }
        }

        // 1. Kick the Kernel (send syscall)
        tx_bridge->poll_tx(*domain);

        // 2. Robust Polling for Kernel Delivery
        uint64_t start_count = core_logic.verified_count;
        bool success = false;

        for (int retry = 0; retry < 5000; ++retry) {
            rx_bridge->poll_rx(*domain, core_logic);
            if (core_logic.verified_count > start_count) {
                success = true;
                break;
            }
            std::atomic_signal_fence(std::memory_order_acq_rel);
        }

        if (!success) {
            state.SkipWithError("Kernel Timeout: Socket failed to deliver data.");
            break;
        }
    }

    state.SetItemsProcessed(core_logic.verified_count);

    slabflux::os::close_socket(tx_fd);
    slabflux::os::close_socket(rx_fd);
}

BENCHMARK(BM_NetworkConduit_KernelTax)->Unit(benchmark::kNanosecond);
BENCHMARK_MAIN();
