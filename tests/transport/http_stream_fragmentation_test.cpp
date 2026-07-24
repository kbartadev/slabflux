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
 * ============================================================================*
 * @file http_stream_fragmentation_test.cpp
 * @brief Verifies HTTP parser behavior under split-packet / Slowloris-style ingestion.
 */

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "slabflux/transport/http_frame.hpp"
#include "slabflux/transport/baremetal_parser.hpp"

using namespace slabflux::transport;

namespace {

const char* k_full_request =
    "GET /tensor HTTP/1.1\r\n"
    "Host: hft.local\r\n"
    "X-Trace: abc\r\n"
    "\r\n";

http_frame make_event() {
    http_frame evt{};
    evt.reset(); // Ensure clean state for the new parser
    return evt;
}

}  // namespace

TEST(HttpStreamChaos, IncompleteRequestLineRejected) {
    const char* prefix = "GET /tensor HT";
    http_frame evt = make_event();
    EXPECT_EQ(baremetal_parser::parse(std::string_view(prefix, std::strlen(prefix)), evt), parser_status::INCOMPLETE);
}

TEST(HttpStreamChaos, IncompleteHeaderBlockRejected) {
    const char* prefix =
        "GET /tensor HTTP/1.1\r\n"
        "Host: hft.local\r\n"
        "X-Trace: ab";
    http_frame evt = make_event();
    EXPECT_EQ(baremetal_parser::parse(std::string_view(prefix, std::strlen(prefix)), evt), parser_status::INCOMPLETE);
}

TEST(HttpStreamChaos, CompleteBufferAccepted) {
    http_frame evt = make_event();
    EXPECT_EQ(baremetal_parser::parse(std::string_view(k_full_request, std::strlen(k_full_request)), evt), parser_status::OK);
    EXPECT_EQ(evt.method, "GET");
    EXPECT_EQ(evt.uri, "/tensor");
    EXPECT_TRUE(evt.body.empty());
}

TEST(HttpStreamChaos, IncrementalDeliveryMatchesFullParseAtCompletion) {
    const std::string req = k_full_request;
    http_frame reference = make_event();
    ASSERT_EQ(baremetal_parser::parse(std::string_view(req.data(), req.size()), reference), parser_status::OK);

    std::string acc;
    http_frame incremental = make_event();
    bool saw_success = false;

    for (size_t i = 0; i < req.size(); ++i) {
        acc.push_back(req[i]);
        http_frame evt = make_event();
        const bool ok = (baremetal_parser::parse(std::string_view(acc.data(), acc.size()), evt) == parser_status::OK);
        if (ok) {
            saw_success = true;
            EXPECT_EQ(evt.method, reference.method);
            EXPECT_EQ(evt.uri, reference.uri);
            EXPECT_EQ(evt.header_count, reference.header_count);
        }
    }

    EXPECT_TRUE(saw_success);
}

TEST(HttpStreamChaos, TruncatedHeaderLineRejected) {
    // Matrix Realignment: Removing the colon turns this into an invalid header name 
    // terminated by a line-break, triggering the protocol violation check.
    const char* truncated = "GET /x HTTP/1.1\r\nHost-no-colon-here\r";
    http_frame evt = make_event();
    EXPECT_EQ(baremetal_parser::parse(std::string_view(truncated, std::strlen(truncated)), evt), parser_status::ERROR);
}
