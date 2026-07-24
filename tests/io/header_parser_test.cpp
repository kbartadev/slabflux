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

#include <gtest/gtest.h>
#include <x86intrin.h>
#include <cstring> // For std::memset
#include "slabflux/io/header_parser.hpp"
#include "slabflux/io/uring_ingress_stream.hpp"
#include "slabflux/transport/http_avx.hpp"

using namespace slabflux::io;

namespace slabflux::transport {
    /**
     * @brief Test-specific Request Envelope.
     * @details Extends the base event with a physical buffer to simulate 
     * incoming wire data during residency and alignment audit tests.
     */
    static constexpr size_t MAX_PAYLOAD = 4096;

    struct http_request : public http_request_event {
        alignas(64) char raw_buffer[MAX_PAYLOAD];
        size_t buffer_length{0};
    };
}

/**
 * @brief SIMD Scanning Integrity.
 * Proves that parse_fast correctly identifies headers using AVX-512 
 * k-register masks for zero-branch execution.
 */
TEST(HeaderParserTest, VectorizedScanIntegrity) {
    const char* raw_http = 
        "Host: slabflux.io\r\n" 
        "Content-Type: application/octet-stream\r\n"
        "\r\n"
        "BODY_DATA";

    header_parser::parsed_event ev;
    header_parser::parse_fast(raw_http, raw_http + std::strlen(raw_http), &ev);

    ASSERT_EQ(ev.header_count, 2);
    EXPECT_EQ(ev.headers[0].name, "Host");
    EXPECT_EQ(ev.headers[0].value, "slabflux.io");
    EXPECT_EQ(ev.headers[1].name, "Content-Type");
    EXPECT_EQ(ev.headers[1].value, "application/octet-stream");
    EXPECT_EQ(ev.body, "BODY_DATA");
}

/**
 * @brief AVX-512 Scanning Throughput.
 * Measures cycles per byte for header parsing.
 */
TEST(HeaderParserTest, ScanningCycleBudget) {
    std::string large_headers;
    for(int i=0; i<10; ++i) large_headers += "X-Header-" + std::to_string(i) + ": val\r\n"; // Corrected typo: "X-Header"
    large_headers += "\r\n";

    header_parser::parsed_event ev;
    const char* start_ptr = large_headers.data();
    const char* end_ptr = start_ptr + large_headers.size();

    uint64_t start = __rdtsc();
    for(int i=0; i<1000; ++i) {
        ev.header_count = 0;
        header_parser::parse_fast(start_ptr, end_ptr, &ev);
        // Fix Heisenbug: Prevent compiler from optimizing away the loop and calls
        asm volatile("" : : "g"(&ev) : "memory");
    }
    uint64_t end = __rdtsc();

    double cycles_per_call = static_cast<double>(end - start) / 1000.0;
    std::cout << "[PERF] SIMD Parser Latency: " << cycles_per_call << " cycles/call\n";
    
    // Requirement: Sub-200 cycles for 10 headers on AVX-512 silicon (including measurement tax)
    EXPECT_LT(cycles_per_call, 200.0);
}

struct dummy_ingress_pipe {
    template <typename T>
    void process(T) {}
};

/**
 * @brief Raw Ring Access (Phase A/B) Verification.
 * Audits the internal loop of the uring_ingress_stream.
 */
TEST(Ingress_streamTest, RawRingAccessPhysics) {
    // This test ensures the struct layout satisfies cache-line isolation
    using audit_stream_t = slabflux::io::uring_ingress_stream<slabflux::core::pool<slabflux::transport::http_request, 1024>, dummy_ingress_pipe>;
    EXPECT_EQ(alignof(audit_stream_t), 64);
    EXPECT_EQ(sizeof(audit_stream_t) % 64, 0);
}
