/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * @file http_shared_test.cpp
 * @brief Strict Equivalence Test Harness for Scalar vs SIMD HTTP Parsers.
 */

#include <gtest/gtest.h>
#include "slabflux/transport/http.hpp"
#include "slabflux/transport/http_avx.hpp"
#include <string>
#include <cstring>

using namespace slabflux::transport;

// Wrappers to normalize the call signature for the Typed Test Suite
struct ScalarWrapper {
    static bool parse(const char* raw, size_t len, http_request_event& ev) {
        return http_parser::parse(raw, len, ev);
    }
};

struct AvxWrapper {
    static bool parse(const char* raw, size_t len, http_request_event& ev) {
        return http_avx_parser::parse(raw, len, ev);
    }
};

template <typename ParserT>
class HttpSharedTest : public ::testing::Test {};

using ParserImplementations = ::testing::Types<ScalarWrapper, AvxWrapper>;
TYPED_TEST_SUITE(HttpSharedTest, ParserImplementations);

TYPED_TEST(HttpSharedTest, StandardGetRequest) {
    const char* req = "GET /api/v1/status HTTP/1.1\r\nHost: test.com\r\nAccept: */*\r\n\r\n";
    http_request_event ev{};
    bool ok = TypeParam::parse(req, std::strlen(req), ev);
    
    ASSERT_TRUE(ok);
    EXPECT_EQ(ev.method, "GET");
    EXPECT_EQ(ev.uri, "/api/v1/status");
    EXPECT_EQ(ev.header_count, 2);
    EXPECT_EQ(ev.headers[0].key, "Host");
    EXPECT_EQ(ev.headers[0].value, "test.com");
    EXPECT_EQ(ev.headers[1].key, "Accept");
    EXPECT_EQ(ev.headers[1].value, "*/*");
    EXPECT_TRUE(ev.body.empty());
    EXPECT_TRUE(ev.keep_alive);
    EXPECT_EQ(ev.bytes_consumed, std::strlen(req));
}

TYPED_TEST(HttpSharedTest, PostWithBody) {
    const char* req = "POST /data HTTP/1.1\r\nContent-Length: 5\r\n\r\nHello";
    http_request_event ev{};
    bool ok = TypeParam::parse(req, std::strlen(req), ev);
    
    ASSERT_TRUE(ok);
    EXPECT_EQ(ev.method, "POST");
    EXPECT_EQ(ev.uri, "/data");
    EXPECT_EQ(ev.body, "Hello");
    EXPECT_EQ(ev.content_length, 5);
    EXPECT_EQ(ev.bytes_consumed, std::strlen(req));
}

TYPED_TEST(HttpSharedTest, ConnectionClose_Parsed) {
    const char* req = "GET / HTTP/1.1\r\nConnection: close\r\n\r\n";
    http_request_event ev{};
    bool ok = TypeParam::parse(req, std::strlen(req), ev);
    
    ASSERT_TRUE(ok);
    EXPECT_FALSE(ev.keep_alive);
}

TYPED_TEST(HttpSharedTest, ContentLength_Smuggling_Rejected) {
    // Prevents "5 evil" from being parsed as "5" 
    const char* req = "POST / HTTP/1.1\r\nContent-Length: 5 evil\r\n\r\nHello";
    http_request_event ev{};
    bool ok = TypeParam::parse(req, std::strlen(req), ev);
    EXPECT_FALSE(ok);
}

TYPED_TEST(HttpSharedTest, PipelinedRequests) {
    const char* req = "GET /1 HTTP/1.1\r\nHost: a\r\n\r\nGET /2 HTTP/1.1\r\nHost: b\r\nConnection: close\r\n\r\n";
    http_request_event ev1{}, ev2{};
    
    // First request
    bool ok1 = TypeParam::parse(req, std::strlen(req), ev1);
    ASSERT_TRUE(ok1);
    EXPECT_EQ(ev1.uri, "/1");
    EXPECT_TRUE(ev1.keep_alive);
    EXPECT_GT(ev1.bytes_consumed, 0);
    EXPECT_LT(ev1.bytes_consumed, std::strlen(req));

    // Pipelined Request slice
    bool ok2 = TypeParam::parse(req + ev1.bytes_consumed, std::strlen(req) - ev1.bytes_consumed, ev2);
    ASSERT_TRUE(ok2);
    EXPECT_EQ(ev2.uri, "/2");
    EXPECT_FALSE(ev2.keep_alive);
}

TYPED_TEST(HttpSharedTest, MethodOnly_Fails) {
    const char* req = "GET ";
    http_request_event ev{};
    bool ok = TypeParam::parse(req, std::strlen(req), ev);
    EXPECT_FALSE(ok);
}

TYPED_TEST(HttpSharedTest, CRLFInjection_Rejected) {
    const char* req = "GET / HTTP/1.1\r\nHost: evil\r\ninjected: x\r\n\r\n";
    http_request_event ev{};
    bool ok = TypeParam::parse(req, std::strlen(req), ev);
    EXPECT_FALSE(ok);
}

TYPED_TEST(HttpSharedTest, ChunkedEncoding_Rejected) {
    const char* req = "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n1\r\na\r\n0\r\n\r\n";
    http_request_event ev{};
    bool ok = TypeParam::parse(req, std::strlen(req), ev);
    EXPECT_FALSE(ok);
}

TYPED_TEST(HttpSharedTest, StrayLF_AllowedAndTrimmed) {
    const char* req = "GET / HTTP/1.1\nHost: \t test \t\n\n";
    http_request_event ev{};
    bool ok = TypeParam::parse(req, std::strlen(req), ev);
    
    ASSERT_TRUE(ok);
    EXPECT_EQ(ev.header_count, 1);
    EXPECT_EQ(ev.headers[0].key, "Host");
    EXPECT_EQ(ev.headers[0].value, "test"); // OWS trimming check
}

TYPED_TEST(HttpSharedTest, SpaceInHeaderKey_Rejected) {
    const char* req = "GET / HTTP/1.1\r\nInvalid Key: value\r\n\r\n";
    http_request_event ev{};
    bool ok = TypeParam::parse(req, std::strlen(req), ev);
    EXPECT_FALSE(ok);
}

TYPED_TEST(HttpSharedTest, LongPayload_ZeroCopyBoundary) {
    // Generate an exact 4096-byte request to test SIMD chunk boundaries
    alignas(64) char req_buf[8192] = "POST / HTTP/1.1\r\nHost: big\r\nContent-Length: 4000\r\n\r\n";
    std::string req = req_buf;
    req.append(4000, 'A');
    std::memcpy(req_buf, req.data(), req.size());
    
    http_request_event ev{};
    bool ok = TypeParam::parse(req_buf, req.size(), ev);
    
    ASSERT_TRUE(ok);
    EXPECT_EQ(ev.header_count, 2);
    EXPECT_EQ(ev.body.size(), 4000);
    EXPECT_EQ(ev.body.back(), 'A');
}