
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
 * ============================================================================*/#include <gtest/gtest.h>
#include "slabflux/transport/http.hpp"

using namespace slabflux::transport;

TEST(HttpParser, ParsesSimpleGetRequest) {
    const char* raw =
        "GET /hello HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "User-Agent: test\r\n"
        "\r\n";

    http_request_event evt{};
    bool ok = http_parser::parse(raw, strlen(raw), evt);

    EXPECT_TRUE(ok);
    EXPECT_EQ(evt.method, "GET");
    EXPECT_EQ(evt.uri, "/hello");

    ASSERT_EQ(evt.header_count, 2);
    EXPECT_EQ(evt.headers[0].key, "Host");
    EXPECT_EQ(evt.headers[0].value, "example.com");
    EXPECT_EQ(evt.headers[1].key, "User-Agent");
    EXPECT_EQ(evt.headers[1].value, "test");
}

TEST(HttpParser, RejectsTooManyHeaders) {
    std::string req = "GET /x HTTP/1.1\r\n";

    for (int i = 0; i < http_request_event::MAX_HEADERS + 5; i++) {
        req += "X-H" + std::to_string(i) + ": v\r\n";
    }
    req += "\r\n";

    http_request_event evt{};
    bool ok = http_parser::parse(req.c_str(), req.size(), evt);

    EXPECT_FALSE(ok);
}

TEST(HttpParser, AcceptsLFOnly) {
    const char* raw =
        "GET /x HTTP/1.1\n"
        "Host: a\n"
        "\n";

    http_request_event evt{};
    bool ok = http_parser::parse(raw, strlen(raw), evt);

    EXPECT_TRUE(ok);
    EXPECT_EQ(evt.method, "GET");
    EXPECT_EQ(evt.uri, "/x");
    EXPECT_EQ(evt.header_count, 1);
    EXPECT_EQ(evt.headers[0].key, "Host");
    EXPECT_EQ(evt.headers[0].value, "a");
}

TEST(HttpParser, TrimsHeaderValueSpaces) {
    const char* raw =
        "GET /x HTTP/1.1\r\n"
        "Host:    example.com   \r\n"
        "\r\n";

    http_request_event evt{};
    bool ok = http_parser::parse(raw, strlen(raw), evt);

    EXPECT_TRUE(ok);
    EXPECT_EQ(evt.headers[0].key, "Host");
    EXPECT_EQ(evt.headers[0].value, "example.com");
}

TEST(HttpParser, AllowsEmptyHeaderValue) {
    const char* raw =
        "GET /x HTTP/1.1\r\n"
        "X-Test:\r\n"
        "\r\n";

    http_request_event evt{};
    bool ok = http_parser::parse(raw, strlen(raw), evt);

    EXPECT_TRUE(ok);
    EXPECT_EQ(evt.header_count, 1);
    EXPECT_EQ(evt.headers[0].key, "X-Test");
    EXPECT_EQ(evt.headers[0].value, "");
}

TEST(HttpParser, NoBody) {
    const char* raw =
        "GET /x HTTP/1.1\r\n"
        "Host: a\r\n"
        "\r\n";

    http_request_event evt{};
    bool ok = http_parser::parse(raw, strlen(raw), evt);

    EXPECT_TRUE(ok);
    EXPECT_EQ(evt.body.size(), 0);
}

TEST(HttpParser, LargeBodyZeroCopy) {
    std::string body(5000, 'A');

    std::string req =
        "POST /upload HTTP/1.1\r\n"
        "Host: x\r\n"
        "Content-Length: 5000\r\n"
        "\r\n" +
        body;

    http_request_event evt{};
    bool ok = http_parser::parse(req.data(), req.size(), evt);

    EXPECT_TRUE(ok);
    EXPECT_EQ(evt.body.size(), body.size());
    EXPECT_EQ(evt.body[0], 'A');
    EXPECT_EQ(evt.body.back(), 'A');
}

TEST(HttpParser, RejectsWhitespaceInHeaderKey) {
    const char* raw =
        "GET /x HTTP/1.1\r\n"
        "Bad Key: x\r\n"
        "\r\n";

    http_request_event evt{};
    bool ok = http_parser::parse(raw, strlen(raw), evt);

    EXPECT_FALSE(ok);
}

TEST(HttpParser, RejectsMissingURI) {
    const char* raw =
        "GET  HTTP/1.1\r\n"
        "Host: x\r\n"
        "\r\n";

    http_request_event evt{};
    bool ok = http_parser::parse(raw, strlen(raw), evt);

    EXPECT_FALSE(ok);
}

TEST(HttpParser, RejectsHeaderWithoutColon) {
    const char* raw =
        "GET /x HTTP/1.1\r\n"
        "Host x\r\n"
        "\r\n";

    http_request_event evt{};
    bool ok = http_parser::parse(raw, strlen(raw), evt);

    EXPECT_FALSE(ok);
}

TEST(HttpParser, RejectsEmptyHeaderKey) {
    const char* raw =
        "GET /x HTTP/1.1\r\n"
        ": value\r\n"
        "\r\n";

    http_request_event evt{};
    bool ok = http_parser::parse(raw, strlen(raw), evt);

    EXPECT_FALSE(ok);
}

TEST(HttpParser, AcceptsMultipleBlankLinesBeforeBody) {
    const char* raw =
        "POST /x HTTP/1.1\r\n"
        "Host: x\r\n"
        "Content-Length: 6\r\n"
        "\r\n"
        "\r\n"
        "DATA";

    http_request_event evt{};
    bool ok = http_parser::parse(raw, strlen(raw), evt);

    EXPECT_TRUE(ok);
    EXPECT_EQ(evt.body, "\r\nDATA");
}

TEST(HttpParser, RejectsInvalidHeaderKeyCharacters) {
    const char* raw =
        "GET /x HTTP/1.1\r\n"
        "Ho st: x\r\n"
        "\r\n";

    http_request_event evt{};
    bool ok = http_parser::parse(raw, strlen(raw), evt);

    EXPECT_FALSE(ok);
}

TEST(HttpParser, RejectsCRLFInjectionInHeaderValue) {
    const char* raw =
        "GET /x HTTP/1.1\r\n"
        "Host: evil\r\ninjected: x\r\n"
        "\r\n";

    http_request_event evt{};
    bool ok = http_parser::parse(raw, strlen(raw), evt);

    EXPECT_FALSE(ok);
}

TEST(HttpParser, RejectsHeaderWithEmptyKey) {
    const char* raw =
        "GET /x HTTP/1.1\r\n"
        ": value\r\n"
        "\r\n";

    http_request_event evt{};
    EXPECT_FALSE(http_parser::parse(raw, strlen(raw), evt));
}

TEST(HttpParser, RejectsHeaderValueStartingWithCR) {
    const char* raw =
        "GET /x HTTP/1.1\r\n"
        "Host:\rvalue\r\n"
        "\r\n";

    http_request_event evt{};
    EXPECT_FALSE(http_parser::parse(raw, strlen(raw), evt));
}

TEST(HttpParser, AllowsDuplicateHeaders) {
    const char* raw =
        "GET /x HTTP/1.1\r\n"
        "X-A: 1\r\n"
        "X-A: 2\r\n"
        "\r\n";

    http_request_event evt{};
    bool ok = http_parser::parse(raw, strlen(raw), evt);

    EXPECT_TRUE(ok);
    EXPECT_EQ(evt.header_count, 2);
    EXPECT_EQ(evt.headers[0].value, "1");
    EXPECT_EQ(evt.headers[1].value, "2");
}

TEST(HttpParser, AcceptsMixedCaseHeaderKeys) {
    const char* raw =
        "GET /x HTTP/1.1\r\n"
        "HoSt: x\r\n"
        "\r\n";

    http_request_event evt{};
    bool ok = http_parser::parse(raw, strlen(raw), evt);

    EXPECT_TRUE(ok);
    EXPECT_EQ(evt.headers[0].key, "HoSt");
}

TEST(HttpParser, AcceptsBinaryBody) {
    const char raw[] =
        "POST /bin HTTP/1.1\r\n"
        "Host: x\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "\x01\x02\x03\xFF\x00";

    http_request_event evt{};
    bool ok = http_parser::parse(raw, sizeof(raw), evt);

    EXPECT_TRUE(ok);
    EXPECT_EQ(evt.body.size(), 5);
    EXPECT_EQ((unsigned char)evt.body[3], 0xFF);
}

TEST(HttpParser, AcceptsHeaderEndingWithLFOnly) {
    const char* raw =
        "GET /x HTTP/1.1\n"
        "Host: x\n"
        "\n";

    http_request_event evt{};
    EXPECT_TRUE(http_parser::parse(raw, strlen(raw), evt));
}

TEST(HttpParser, RejectsMissingHTTPVersion) {
    const char* raw =
        "GET /x\r\n"
        "Host: x\r\n"
        "\r\n";

    http_request_event evt{};
    EXPECT_FALSE(http_parser::parse(raw, strlen(raw), evt));
}

TEST(HttpParser, RejectsHeaderKeyTooLong) {
    std::string longkey(300, 'A');

    std::string raw =
        "GET /x HTTP/1.1\r\n" +
        longkey + ": x\r\n\r\n";

    http_request_event evt{};
    EXPECT_FALSE(http_parser::parse(raw.data(), raw.size(), evt));
}

TEST(HttpParser, AcceptsWhitespaceAfterHeaderSection) {
    const char* raw =
        "GET /x HTTP/1.1\r\n"
        "Host: x\r\n"
        "Content-Length: 3\r\n"
        "\r\n"
        "   ";

    http_request_event evt{};
    bool ok = http_parser::parse(raw, strlen(raw), evt);

    EXPECT_TRUE(ok);
    EXPECT_EQ(evt.body, "   ");
}

TEST(HttpParser, TrimsMultipleSpacesInHeaderValue) {
    const char* raw =
        "GET /x HTTP/1.1\r\n"
        "Host:     example.com     \r\n"
        "\r\n";

    http_request_event evt{};
    bool ok = http_parser::parse(raw, strlen(raw), evt);

    EXPECT_TRUE(ok);
    EXPECT_EQ(evt.headers[0].value, "example.com");
}

TEST(HttpParser, RejectsHeaderKeyWithTrailingSpace) {
    const char* raw =
        "GET /x HTTP/1.1\r\n"
        "Host : x\r\n"
        "\r\n";

    http_request_event evt{};
    EXPECT_FALSE(http_parser::parse(raw, strlen(raw), evt));
}

TEST(HttpParser, RejectsChunkedEncoding) {
    const char* raw =
        "POST /x HTTP/1.1\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "4\r\n"
        "Wiki\r\n"
        "0\r\n\r\n";

    http_request_event evt{};
    EXPECT_FALSE(http_parser::parse(raw, strlen(raw), evt));
}

TEST(HttpParser, RejectsUTF8HeaderKey) {
    const char* raw =
        "GET /x HTTP/1.1\r\n"
        "Ü-Key: x\r\n"
        "\r\n";

    http_request_event evt{};
    EXPECT_FALSE(http_parser::parse(raw, strlen(raw), evt));
}
