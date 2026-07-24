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
 * ============================================================================* @file 04_cache_line_isolation.cpp
 * @brief Cache line isolation example for stateful handlers.
 */
#include <array>
#include <iostream>

#include "slabflux/core.hpp"

using namespace slabflux;
using namespace slabflux::core;

struct trade_tick {
    int symbol_id;
    double price;
    trade_tick(int id, double p) : symbol_id(id), price(p) {}
};

// Stateful handler with cache-line isolation.
// alignas(CACHE_LINE_SIZE) ensures the handler's internal state never shares
// a cache line with the memory pool or conduits — no false sharing, no bleed.
struct alignas(CACHE_LINE_SIZE) order_book_handler {
    std::array<double, 10> last_prices{};
    int total_trades = 0;

    void on(const trade_tick& ev) {
        // O(1) state update for the affected symbol.
        if (ev.symbol_id >= 0 && ev.symbol_id < 10) {
            last_prices[ev.symbol_id] = ev.price;
            total_trades++;
        }
    }

    void print_snapshot() const {
        std::cout << "--- Order Book Snapshot (" << total_trades << " trades) ---\n";
        for (int i = 0; i < 3; ++i) {
            std::cout << "Symbol " << i << " last price: $" << last_prices[i] << "\n";
        }
    }
};

int main() {
    spsc_pool<trade_tick, 1024> pool;
    order_book_handler ob;

    ob.on(*pool.make(0, 150.25).release());
    ob.on(*pool.make(1, 2800.00).release());
    ob.on(*pool.make(0, 150.30).release());

    ob.print_snapshot();
    return 0;
}
