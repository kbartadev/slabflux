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
 * @file baremetal_parser_test.cpp
 * @brief Unit tests for the Baremetal DFA Parser.
 */

#include <gtest/gtest.h>
#include <string_view>
#include "slabflux/transport/baremetal_parser.hpp"
#include "slabflux/transport/http_frame.hpp"

using namespace slabflux::transport;

TEST(BaremetalParserTest, BasicGetRequestParsing) {
    std::string_view req = "GET /index.html HTTP/1.1\r\nHost: localhost\r\nContent-Length: 5\r\n\r\nHELLO";
    http_frame frame;
    frame.reset();

    parser_status status = baremetal_parser::parse(req, frame);
    
    EXPECT_EQ(status, parser_status::OK);
    EXPECT_EQ(frame.method, "GET");
    EXPECT_EQ(frame.uri, "/index.html");
    EXPECT_EQ(frame.version, "HTTP/1.1");
    EXPECT_EQ(frame.header_count, 2);
    EXPECT_EQ(frame.content_length, 5);
    EXPECT_EQ(frame.body, "HELLO");
}

TEST(BaremetalParserTest, IncompleteParsing) {
    std::string_view req = "GET /index.html HTT";
    http_frame frame;
    frame.reset();

    parser_status status = baremetal_parser::parse(req, frame);
    EXPECT_EQ(status, parser_status::INCOMPLETE);
    
    // Verify consumption tracking
    EXPECT_GT(frame.total_bytes_consumed, 0);
    EXPECT_EQ(frame.internal_state, parser_state::VERSION);
}