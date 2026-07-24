/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * @file http2_hpack_test.cpp
 * @brief Unit tests for the HPACK (HTTP/2 Header Compression) decoder.
 */

#include <gtest/gtest.h>
#include "slabflux/transport/hpack.hpp"

using namespace slabflux::transport;

TEST(HpackTest, DecodeInteger) {
    uint64_t value;
    // Example from RFC 7541 C.1.1
    std::string_view buf1("\x0a", 1);
    ASSERT_TRUE(hpack_decoder::decode_integer(buf1, 5, value));
    EXPECT_EQ(value, 10);
    EXPECT_TRUE(buf1.empty());

    // Example from RFC 7541 C.1.2
    std::string_view buf2("\x1f\x9a\x0a", 3);
    ASSERT_TRUE(hpack_decoder::decode_integer(buf2, 5, value));
    EXPECT_EQ(value, 1337);
    EXPECT_TRUE(buf2.empty());

    // Example from RFC 7541 C.1.3
    std::string_view buf3("\x2a", 1);
    ASSERT_TRUE(hpack_decoder::decode_integer(buf3, 8, value));
    EXPECT_EQ(value, 42);
    EXPECT_TRUE(buf3.empty());
}

TEST(HpackTest, DecodeStringLiteral) {
    hpack_decoder decoder;
    std::string_view value;
    // Example from RFC 7541 C.2.1
    // The string "custom-key" is 10 bytes long, so the length prefix is 0x0a.
    std::string_view buf1("\x0a" "custom-key", 11);
    ASSERT_TRUE(decoder.decode_string(buf1, value));
    EXPECT_EQ(value, "custom-key");
    EXPECT_TRUE(buf1.empty());
}

TEST(HpackTest, DecodeIndexedHeaderField) {
    hpack_decoder decoder;
    std::vector<hpack_header> headers;
    // Example from RFC 7541 C.3.1: :method: GET
    std::string_view buf("\x82", 1);
    ASSERT_TRUE(decoder.decode(buf, headers));
    ASSERT_EQ(headers.size(), 1);
    EXPECT_EQ(headers[0].name, ":method");
    EXPECT_EQ(headers[0].value, "GET");
}

TEST(HpackTest, DecodeLiteralHeaderWithIndexing) {
    hpack_decoder decoder;
    std::vector<hpack_header> headers;
    // Example from RFC 7541 C.4.1: Literal Header Field with Incremental Indexing -- New Name
    std::string_view buf("\x40\x0a" "custom-key" "\x0c" "custom-value", 25);
    ASSERT_TRUE(decoder.decode(buf, headers));
    ASSERT_EQ(headers.size(), 1);
    EXPECT_EQ(headers[0].name, "custom-key");
    EXPECT_EQ(headers[0].value, "custom-value");
}