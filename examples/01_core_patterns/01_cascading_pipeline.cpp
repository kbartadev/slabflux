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
 * ============================================================================* @file 01_cascading_pipeline.cpp
 * @brief Cascading pipeline executing multi-layer events via C++ pure inheritance (4D Matrix).
 */
#include <iostream>
#include <string_view>
#include "slabflux/core.hpp"

using namespace slabflux;

// --- 1. Multi-layer event composed from independent logical layers.
//     Composition is used instead of OOP inheritance to avoid fragmentation
//     and to remain compatible with O(1) slab-based allocation.
struct net_layer {
    std::string_view ip;
};
struct auth_layer {
    int session_id;
};
struct app_layer {
    std::string_view payload;
};

// 2. The Event
struct http_request {
    net_layer net;
    auth_layer auth;
    app_layer app;

    http_request(std::string_view ip_addr, int sid, std::string_view p)
    {
        this->net.ip = ip_addr;
        this->auth.session_id = sid;
        this->app.payload = p;
    }
};

// --- 2. Independent, specialized handlers.
//     Each stage operates only on its own layer and has no visibility into others.
struct firewall_stage {
    void on(net_layer& net) { std::cout << "[Firewall] Checking IP: " << net.ip << "\n"; }
};

struct auth_stage {
    void on(auth_layer& auth) {
        std::cout << "[Auth] Validating session: " << auth.session_id << "\n";
    }
};

// --- 3. Cascading processor that defines the strict execution order.
//     The sequence is explicit and fully inlined at compile time.
struct request_processor {
    firewall_stage fw;
    auth_stage au;

    void on(http_request* ev) { // Changed to raw pointer
        if (!ev) return;
        std::cout << "--- Processing new request ---\n";

        // Strict waterfall-style ordering (compile-time inlined).
        fw.on(ev->net);
        au.on(ev->auth);

        // Final application-level processing.
        std::cout << "[App] Processing payload: " << ev->app.payload << "\n";
    }
};

int main() {
    runtime_domain<http_request> domain;
    request_processor processor;

    // The pipeline contains only the top-level processor,
    // which internally drives all subordinate stages.
    pipeline<request_processor> pipe(processor);

    // Two complex events allocated and processed in O(1).
    auto req1 = domain.make<http_request>("192.168.1.100", 404, "GET /index.html");
    pipe.dispatch(req1); // dispatch now accepts T*

    auto req2 = domain.make<http_request>("10.0.0.5", 200, "POST /login");
    pipe.dispatch(req2); // dispatch now accepts T*

    return 0;
}
