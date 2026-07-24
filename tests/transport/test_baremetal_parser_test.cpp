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
#include "slabflux/transport/baremetal_parser.hpp"

using namespace slabflux::transport;

/**
 * @brief Verifies that the parser correctly extracts tokens from a standard GET request.
 */
TEST(TransportTest, BaremetalParserHandlesStandardGet) {
    std::string_view raw = "GET /api/v1/alpha HTTP/1.1\r\n"
                           "Host: slabflux.local\r\n"
                           "Content-Length: 0\r\n\r\n";
    
    http_frame frame;
    auto status = baremetal_parser::parse(raw, frame);

    EXPECT_EQ(status, parser_status::OK);
    EXPECT_EQ(frame.method, "GET");
    EXPECT_EQ(frame.uri, "/api/v1/alpha");
    EXPECT_EQ(frame.header_count, 2);
    EXPECT_EQ(frame.headers[0].key, "Host");
    EXPECT_EQ(frame.headers[0].value, "slabflux.local");
}

/**
 * @brief Verifies that partial frames are detected as INCOMPLETE.
 */
TEST(TransportTest, BaremetalParserHandlesIncompleteFrame) {
    std::string_view partial = "GET /api/v1/alpha HTT";
    
    http_frame frame;
    auto status = baremetal_parser::parse(partial, frame);

    EXPECT_EQ(status, parser_status::INCOMPLETE);
}

/**
 * @brief Verifies that protocol violations return ERROR.
 */
TEST(TransportTest, BaremetalParserDetectsMalformedLineEndings) {
    std::string_view malformed = "GET /uri HTTP/1.1\rX";
    http_frame frame;
    EXPECT_EQ(baremetal_parser::parse(malformed, frame), parser_status::ERROR);
}

/**
 * @brief Verifies protection against header capacity exhaustion.
 */
TEST(TransportTest, BaremetalParserEnforcesHeaderLimit) {
    std::string raw = "GET / HTTP/1.1\r\n";
    for (int i = 0; i < 33; ++i) {
        raw += "Key: Value\r\n";
    }
    raw += "\r\n";

    http_frame frame;
    auto status = baremetal_parser::parse(raw, frame);
    
    EXPECT_EQ(status, parser_status::ERROR);
}

/**
 * @brief Verifies that the parser extracts Content-Length and isolates the body.
 */
TEST(TransportTest, BaremetalParserExtractsContentLengthAndBody) {
    std::string_view raw = "POST /data HTTP/1.1\r\n"
                           "Content-Length: 5\r\n\r\n"
                           "HELLO_EXTRA_DATA";
    
    http_frame frame;
    auto status = baremetal_parser::parse(raw, frame);

    EXPECT_EQ(status, parser_status::OK);
    EXPECT_EQ(frame.content_length, 5);
    EXPECT_EQ(frame.body, "HELLO");
    // Verify stream awareness: total_bytes_consumed should not include "_EXTRA_DATA"
    EXPECT_EQ(frame.total_bytes_consumed, raw.find("HELLO") + 5);
}

/**
 * @brief Verifies RFC compliance for case-insensitive headers and whitespace.
 */
TEST(TransportTest, BaremetalParserHandlesRfcEdgeCases) {
    std::string_view raw = "GET / HTTP/1.1\r\n"
                           "X-Slab-Key:\tValue  \r\n" // Tab leading + space trailing
                           "Transfer-Encoding: chunked\r\n\r\n"
                           "5\r\nHELLO\r\n0\r\n\r\n";
    
    http_frame frame;
    EXPECT_EQ(baremetal_parser::parse(raw, frame), parser_status::CHUNK_READY);
    EXPECT_EQ(frame.headers[0].value, "Value");
    EXPECT_TRUE(frame.is_chunked);
    EXPECT_EQ(frame.body, "HELLO");
}

/**
 * @brief Verifies stream-awareness by parsing two concatenated requests.
 */
TEST(TransportTest, BaremetalParserHandlesPipelinedStream) {
    std::string_view stream = "GET /1 HTTP/1.1\r\n\r\n"
                              "GET /2 HTTP/1.1\r\n\r\n";
    
    http_frame f1, f2;
    
    // Parse First
    auto s1 = baremetal_parser::parse(stream, f1);
    EXPECT_EQ(s1, parser_status::OK);
    EXPECT_EQ(f1.uri, "/1");

    // Parse Second starting from consumed offset
    auto remaining = stream.substr(f1.total_bytes_consumed);
    auto s2 = baremetal_parser::parse(remaining, f2);
    EXPECT_EQ(s2, parser_status::OK);
    EXPECT_EQ(f2.uri, "/2");
}

/**
 * @brief Verifies handling of Chunked Transfer-Encoding.
 */
TEST(TransportTest, BaremetalParserHandlesChunkedEncoding) {
    std::string_view raw = "POST /stream HTTP/1.1\r\n"
                           "Transfer-Encoding: chunked\r\n\r\n"
                           "5\r\nHELLO\r\n"
                           "6\r\n WORLD\r\n"
                           "0\r\n\r\n";
    
    http_frame frame;
    frame.reset();
    EXPECT_EQ(baremetal_parser::parse(raw, frame), parser_status::CHUNK_READY); // First chunk
    EXPECT_EQ(frame.body, "HELLO");
    
    EXPECT_EQ(baremetal_parser::parse(raw, frame), parser_status::CHUNK_READY); // Second chunk
    EXPECT_EQ(frame.body, " WORLD");
    
    EXPECT_EQ(baremetal_parser::parse(raw, frame), parser_status::OK); // Final chunk
}

/**
 * @brief Hardening: Verifies conflicting length headers are rejected.
 */
TEST(TransportTest, BaremetalParserRejectsConflictingLengthHeaders) {
    std::string_view raw = "POST /data HTTP/1.1\r\n"
                           "Content-Length: 10\r\n"
                           "Transfer-Encoding: chunked\r\n\r\n";
    http_frame frame;
    EXPECT_EQ(baremetal_parser::parse(raw, frame), parser_status::ERROR);
}

/**
 * @brief Verifies that the parser rejects oversized methods (DoS mitigation).
 */
TEST(TransportTest, BaremetalParserRejectsOversizedMethod) {
    std::string long_method(http_frame::MAX_METHOD_SIZE + 1, 'A');
    std::string raw = long_method + " / HTTP/1.1\r\n\r\n";
    
    http_frame frame;
    EXPECT_EQ(baremetal_parser::parse(raw, frame), parser_status::ERROR);
}

/**
 * @brief Verifies that the parser rejects oversized URIs.
 */
TEST(TransportTest, BaremetalParserRejectsOversizedUri) {
    std::string long_uri(http_frame::MAX_URI_SIZE + 1, '/');
    std::string raw = "GET " + long_uri + " HTTP/1.1\r\n\r\n";
    
    http_frame frame;
    EXPECT_EQ(baremetal_parser::parse(raw, frame), parser_status::ERROR);
}

/**
 * @brief Verifies that the parser rejects oversized header fields.
 */
TEST(TransportTest, BaremetalParserRejectsOversizedHeader) {
    std::string long_key(http_frame::MAX_HEADER_FIELD_SIZE + 1, 'X');
    std::string raw = "GET / HTTP/1.1\r\n" + long_key + ": Value\r\n\r\n";
    
    http_frame frame;
    EXPECT_EQ(baremetal_parser::parse(raw, frame), parser_status::ERROR);

    std::string long_val(http_frame::MAX_HEADER_FIELD_SIZE + 1, 'Y');
    std::string raw2 = "GET / HTTP/1.1\r\nKey: " + long_val + "\r\n\r\n";
    
    frame.reset();
    EXPECT_EQ(baremetal_parser::parse(raw2, frame), parser_status::ERROR);
}

/**
 * @brief Hardening: Verifies total body size limits across multiple chunks.
 */
TEST(TransportTest, BaremetalParserPreventsChunkedAccumulationDos) {
    // Max body is 16MB. We simulate many 1MB chunks.
    std::string raw = "POST /data HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n";
    for(int i = 0; i < 17; ++i) {
        raw += "100000\r\n"; // 1MB hex
        raw.append(1024 * 1024, 'A');
        raw += "\r\n";
    }
    raw += "0\r\n\r\n";

    http_frame frame;
    parser_status status = parser_status::INCOMPLETE;
    size_t chunks_processed = 0;

    // The parser returns CHUNK_READY for each chunk, so we need to loop
    while (status == parser_status::CHUNK_READY || status == parser_status::INCOMPLETE) {
        status = baremetal_parser::parse(raw, frame);
        if (status == parser_status::CHUNK_READY) {
            chunks_processed++;
        } else if (status == parser_status::INCOMPLETE) {
            // Should not happen with a complete raw string
            break;
        }
    }
    
    // Expect an ERROR status due to accumulation limit
    EXPECT_EQ(status, parser_status::ERROR);
    EXPECT_EQ(chunks_processed, 16); // Should process 16 chunks, then error on the 17th
}

/**
 * @brief Verifies that views are reset properly on frame reuse.
 */
TEST(TransportTest, BaremetalParserEnsuresHermeticFrameReuse) {
    http_frame frame;
    
    // First: Full request with body
    std::string_view req1 = "POST /data HTTP/1.1\r\nContent-Length: 5\r\n\r\nHELLO";
    EXPECT_EQ(baremetal_parser::parse(req1, frame), parser_status::OK);
    EXPECT_EQ(frame.body, "HELLO");

    // Second: Simple GET (no body)
    std::string_view req2 = "GET / HTTP/1.1\r\n\r\n";
    frame.reset();
    EXPECT_EQ(baremetal_parser::parse(req2, frame), parser_status::OK);
    EXPECT_TRUE(frame.body.empty()); // Proves req1 body was cleared
}

/**
 * @brief Verifies absolute resumability with fragmented buffers.
 */
TEST(TransportTest, BaremetalParserHandlesExtremeFragmentation) {
    http_frame frame;
    // Simulation: Fragments must be part of a stable, contiguous buffer
    // to avoid dangling string_views during zero-copy parsing.
    std::string buffer = "GET /api/v1 HTTP/1.1\r\nHost: local\r\n\r\n";
    
    EXPECT_EQ(baremetal_parser::parse(std::string_view(buffer).substr(0, 8), frame), parser_status::INCOMPLETE);
    EXPECT_EQ(baremetal_parser::parse(std::string_view(buffer).substr(0, 15), frame), parser_status::INCOMPLETE);
    EXPECT_EQ(baremetal_parser::parse(buffer, frame), parser_status::OK);
    EXPECT_EQ(frame.uri, "/api/v1");
}

/**
 * @brief Verifies the 64-byte alignment of the http_frame for cache-line sovereignty.
 */
TEST(TransportTest, HttpFrameAlignment) {
    EXPECT_EQ(alignof(http_frame), 64);
}
