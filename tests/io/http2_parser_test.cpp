/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 * @file http2_parser_test.cpp
 * @brief Validates HTTP/2 translation into existing HTTP/1.1 Event structures.
 */

#include <gtest/gtest.h>
#include "slabflux/transport/http2_parser.hpp"
#include "slabflux/transport/http.hpp"           // For http_request_event
#include "slabflux/transport/baremetal_parser.hpp" // For http_frame

using namespace slabflux::transport;

TEST(Http2ParserTest, HydratesExistingHttpRequestEvent) {
    http2_parser parser;
    http_request_event ev{}; // From existing HTTP/1.1 AVX Parsers
    
    std::string stream = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    
    // HEADERS Frame: END_HEADERS | END_STREAM (0x05), Stream ID 1
    // HPACK Encoded: :method GET (0x82), :path /api/v1 (0x44, 0x07, "/api/v1"), :authority test.com (0x41, 0x08, "test.com")
    std::string hpack = "\x82\x44\x07/api/v1\x41\x08test.com";
    
    uint32_t len = hpack.size();
    stream.push_back((len >> 16) & 0xFF);
    stream.push_back((len >> 8) & 0xFF);
    stream.push_back(len & 0xFF);
    stream.push_back(0x01); // HEADERS
    stream.push_back(0x05); // END_HEADERS | END_STREAM
    stream.push_back(0x00); stream.push_back(0x00); stream.push_back(0x00); stream.push_back(0x01); // Stream 1
    stream.append(hpack);

    std::string_view view = stream;
    auto status = parser.parse(view, ev);

    // Validation: The HTTP/2 stream directly hydrated the HTTP/1.1 Event Structure!
    // This allows 100% logic reuse for existing Application Routers.
    EXPECT_EQ(status, http2_parser::status::OK);
    EXPECT_EQ(ev.method, "GET");
    EXPECT_EQ(ev.uri, "/api/v1");
    EXPECT_EQ(ev.header_count, 1);
    EXPECT_EQ(ev.headers[0].key, "Host"); 
    EXPECT_EQ(ev.headers[0].value, "test.com");
    EXPECT_TRUE(ev.keep_alive);
    EXPECT_TRUE(view.empty());
}

TEST(Http2ParserTest, HydratesExistingBaremetalFrame) {
    http2_parser parser;
    http_frame ev{}; // From existing Baremetal Parsers
    
    std::string stream = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    
    std::string hpack = "\x83\x44\x05/data"; // :method POST, :path /data
    uint32_t len = hpack.size();
    stream.push_back((len >> 16) & 0xFF); stream.push_back((len >> 8) & 0xFF); stream.push_back(len & 0xFF);
    stream.push_back(0x01); // HEADERS
    stream.push_back(0x04); // END_HEADERS (No END_STREAM)
    stream.push_back(0x00); stream.push_back(0x00); stream.push_back(0x00); stream.push_back(0x01);
    stream.append(hpack);

    // DATA Frame (END_STREAM 0x01)
    std::string data = "Hello World!";
    len = data.size();
    stream.push_back((len >> 16) & 0xFF); stream.push_back((len >> 8) & 0xFF); stream.push_back(len & 0xFF);
    stream.push_back(0x00); // DATA
    stream.push_back(0x01); // END_STREAM
    stream.push_back(0x00); stream.push_back(0x00); stream.push_back(0x00); stream.push_back(0x01);
    stream.append(data);

    std::string_view view = stream;
    auto status = parser.parse(view, ev);

    EXPECT_EQ(status, http2_parser::status::OK);
    EXPECT_EQ(ev.method, "POST");
    EXPECT_EQ(ev.uri, "/data");
    EXPECT_EQ(ev.body, "Hello World!");
    EXPECT_EQ(ev.content_length, 12);
}