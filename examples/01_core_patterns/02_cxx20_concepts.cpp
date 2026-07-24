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
 * ============================================================================* @file 02_cxx20_concepts.cpp
 * @brief Structural recognition using C++20 Concepts and Zero-Context Auto-Gateway.
 */
#include <iostream>
#include "slabflux/meta.hpp"
#include "slabflux/core/pool.hpp"
#include "slabflux/core/pipeline.hpp"

using namespace slabflux;
using namespace slabflux::core;

// Two completely independent events (No common base, zero payload bloat)
// NOTE: Assuming spsc_pool now returns T*
struct market_trade { double price; market_trade(double p) : price(p) {} };
struct fx_quote     { double price; fx_quote(double p) : price(p) {} };

// The C++20 Concept: Anything that has a "price" member variable
template <typename T> concept HasPrice = requires(T a) { a.price; };

// The handler that "latches onto" the concept (Zero inheritance, zero context needed!)
struct pricing_analytics {
    // NOTE: Changed lambda argument and handler 'on' to accept T*
    template <HasPrice E> SLAB_FORCE_INLINE void on(const E* ev) { std::cout << "[Analytics] Processed price: $" << ev->price << "\n"; }
};

int main() {
    spsc_pool<market_trade, 128> trade_pool;
    spsc_pool<fx_quote, 128>     quote_pool;

    pricing_analytics analytics;
    pipeline<pricing_analytics> pipe(analytics);

    // The same pipeline and handler consume the different types seamlessly!
    pipe.dispatch(trade_pool.make(452.10));
    pipe.dispatch(quote_pool.make(1.05));

    return 0;
}
