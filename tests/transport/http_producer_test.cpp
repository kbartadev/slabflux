/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * @file http_producer_test.cpp
 * @brief Equivalence boundary and throughput validation for the HTTP Producer.
 */

#include <gtest/gtest.h>
#include "slabflux/transport/http_producer.hpp"
#include "slabflux/transport/http_avx.hpp"
#include "slabflux/transport/http.hpp"

using namespace slabflux::transport;

TEST(HttpProducerTest, GenerateAndParseStandardRequest) {
    alignas(64) char raw_buffer[4096];
    http_producer builder(raw_buffer, sizeof(raw_buffer));

    // Generate outbound stream
    ASSERT_TRUE(builder.start_request("GET", "/trading/v1/orders"));
    ASSERT_TRUE(builder.add_header("Host", "exchange.local"));
    ASSERT_TRUE(builder.add_header("Authorization", "Bearer x-token"));
    ASSERT_TRUE(builder.add_keep_alive(false));
    ASSERT_TRUE(builder.add_content_length(0));
    ASSERT_TRUE(builder.end_headers());

    std::string_view final_payload = builder.view();

    // Feed immediately to the AVX parser to ensure byte-perfect alignment
    http_request_event ev{};
    bool ok = http_avx_parser::parse(final_payload.data(), final_payload.size(), ev);

    ASSERT_TRUE(ok);
    EXPECT_EQ(ev.method, "GET");
    EXPECT_EQ(ev.uri, "/trading/v1/orders");
    EXPECT_FALSE(ev.keep_alive);
    EXPECT_EQ(ev.content_length, 0);
    EXPECT_EQ(ev.header_count, 4); // Host, Authorization, Connection, Content-Length
    EXPECT_EQ(ev.headers[0].key, "Host");
    EXPECT_EQ(ev.headers[0].value, "exchange.local");
}

TEST(HttpProducerTest, GenerateAndParseResponseWithBody) {
    alignas(64) char raw_buffer[4096];
    http_producer builder(raw_buffer, sizeof(raw_buffer));

    std::string_view json_body = "{\"status\": \"filled\", \"price\": 450.25}";

    ASSERT_TRUE(builder.start_response(200, "OK"));
    ASSERT_TRUE(builder.add_header("Server", "SlabFlux/RTE"));
    ASSERT_TRUE(builder.add_content_length(json_body.size()));
    ASSERT_TRUE(builder.add_keep_alive(true));
    ASSERT_TRUE(builder.end_headers());
    ASSERT_TRUE(builder.append_body(json_body));

    std::string_view final_payload = builder.view();

    // Parse it back utilizing the scalar parser to ensure strict RFC compliance
    http_request_event ev{};
    bool ok = http_parser::parse(final_payload.data(), final_payload.size(), ev);

    // The parser natively processes Responses identically up to the status line
    // "HTTP/1.1" becomes the method, "200" becomes the URI
    ASSERT_TRUE(ok);
    EXPECT_EQ(ev.method, "HTTP/1.1");
    EXPECT_EQ(ev.uri, "200");
    EXPECT_TRUE(ev.keep_alive);
    EXPECT_EQ(ev.content_length, json_body.size());
    EXPECT_EQ(ev.body, json_body);
}

TEST(HttpProducerTest, EnforceCapacityLimits) {
    alignas(64) char tight_buffer[64];
    http_producer builder(tight_buffer, sizeof(tight_buffer));

    // 16 bytes
    ASSERT_TRUE(builder.start_request("GET", "/")); 
    
    // 16 + 21 = 37 bytes
    ASSERT_TRUE(builder.add_header("A", "B")); 
    
    // Exceeds 64 byte capacity bounds
    ASSERT_FALSE(builder.add_header("SuperLongHeaderKey", "With A Very Long Payload Exceeding Capacity"));
}

TEST(HttpProducerTest, GenerateChunkedEncodingStream) {
    alignas(64) char raw_buffer[4096];
    http_producer builder(raw_buffer, sizeof(raw_buffer));

    ASSERT_TRUE(builder.start_response(200, "OK"));
    ASSERT_TRUE(builder.add_chunked_encoding());
    ASSERT_TRUE(builder.end_headers());

    // Simulate piecemeal chunk generation off the hot-path
    ASSERT_TRUE(builder.append_chunk("First slice of telemetry data"));
    ASSERT_TRUE(builder.append_chunk("Second slice of telemetry data"));
    ASSERT_TRUE(builder.end_chunked_stream());

    std::string_view final_payload = builder.view();

    EXPECT_TRUE(final_payload.find("Transfer-Encoding: chunked") != std::string_view::npos);
    EXPECT_TRUE(final_payload.ends_with("0\r\n\r\n"));
}

TEST(HttpProducerTest, GenerateHttp2Frames) {
    alignas(64) char raw_buffer[4096];
    http2_producer builder(raw_buffer, sizeof(raw_buffer));

    ASSERT_TRUE(builder.begin_headers(1));
    ASSERT_TRUE(builder.add_status(200));
    ASSERT_TRUE(builder.add_header("server", "slabflux"));
    ASSERT_TRUE(builder.end_headers(false)); // Headers done, but stream open
    
    ASSERT_TRUE(builder.add_data("Hello HTTP/2", true)); // Data + END_STREAM

    std::string_view final_payload = builder.view();
    EXPECT_GT(final_payload.size(), 18);
    
    // Validate HEADERS frame header
    EXPECT_EQ(final_payload[3], 0x01); // Type = HEADERS
    EXPECT_EQ(final_payload[4], 0x04); // Flags = END_HEADERS
}

TEST(HttpProducerTest, GenerateHttp2ControlFrames) {
    alignas(64) char raw_buffer[4096];
    http2_producer builder(raw_buffer, sizeof(raw_buffer));

    ASSERT_TRUE(builder.add_settings_ack());
    ASSERT_TRUE(builder.add_ping_ack("12345678"));
    ASSERT_TRUE(builder.add_window_update(1, 1024));
    ASSERT_TRUE(builder.add_goaway(0, 0)); // No error
    ASSERT_TRUE(builder.add_rst_stream(1, 8)); // CANCEL

    std::string_view final_payload = builder.view();
    // 9 (SETTINGS) + 17 (PING) + 13 (WINDOW_UPDATE) + 17 (GOAWAY) + 13 (RST_STREAM) = 69 bytes
    EXPECT_EQ(final_payload.size(), 69);
}