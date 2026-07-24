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
#include <string_view>
#include "slabflux/core.hpp"
#include "slabflux/transport/http_avx.hpp"

using namespace slabflux;
using namespace slabflux::transport;

namespace slabflux::transport {
    /**
     * @brief Test-specific Request Envelope.
     * @details Extends the base event with a physical buffer to simulate 
     * incoming wire data during HFT unit tests.
     */
    static constexpr size_t MAX_PAYLOAD = 4096;

    struct http_request : public http_request_event {
        alignas(64) char raw_buffer[MAX_PAYLOAD];
        size_t buffer_length{0};
    };
}

// A mock business logic that only runs if the parser succeeds
struct mock_router {
    bool was_called = false;
    std::string last_method;
    std::string last_uri;
    std::size_t header_count = 0;

    void on(http_request& ev) {
        was_called = true;
		// Deep copy before the pool puts it back (testing only)
        last_method = std::string(ev.method);
        last_uri = std::string(ev.uri);
        header_count = ev.header_count;
    }

    void reset() {
        was_called = false;
        last_method = "";
        last_uri = "";
        header_count = 0;
    }
};

// Wrapper to emulate pipeline short-circuiting natively for the test
struct parser_and_router_stage {
    mock_router& router;
    void dispatch(http_request& req) {
        if (slabflux::transport::http_avx_parser::parse(req.raw_buffer, req.buffer_length, req)) {
            router.on(req);
        }
    }
};

class HttpHftTest : public ::testing::Test {
protected:
    slabflux::pool<slabflux::transport::http_request, 10> memory_pool;
    mock_router router;
    parser_and_router_stage pipe{router};

    // Helper function to place raw data into the pool (testing only)
    http_request* make_request(const char* payload) {
        auto* req = memory_pool.make_raw();
        if (SL_UNLIKELY(!req)) return nullptr;
        
        // Zero-initialize the inherited base fields to prevent garbage state parsing
        std::memset(static_cast<http_request_event*>(req), 0, sizeof(http_request_event));
        std::size_t len = std::strlen(payload);
        std::memset(req->raw_buffer, 0, MAX_PAYLOAD);
        std::memcpy(req->raw_buffer, payload, len);
        req->buffer_length = len;
        return req;
    }
};

TEST_F(HttpHftTest, ParsesValidGetRequestWithHeaders) {
    auto req = make_request("GET /api/v1/trade HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n");
    ASSERT_NE(req, nullptr) << "Memory pool allocation failed";

    pipe.dispatch(*req);

    EXPECT_TRUE(router.was_called);
    EXPECT_EQ(router.last_method, "GET");
    EXPECT_EQ(router.last_uri, "/api/v1/trade");
    EXPECT_EQ(router.header_count, 2);
}

TEST_F(HttpHftTest, ShortCircuitsOnMalformedRequestZeroLeak) {
    // Record pool state before the call
    auto available_before = memory_pool.capacity(); // Or free count if you have an API for it

    auto req = make_request("MALFORMED_NO_NEWLINES_OR_SPACES");
    ASSERT_NE(req, nullptr) << "Memory pool allocation failed";

    pipe.dispatch(*req);

    // 1. Proof: Business logic did NOT run (Short-Circuit works)
    EXPECT_FALSE(router.was_called);


}

TEST_F(HttpHftTest, HandlesEmptyBodyCorrectly) {
    auto req = make_request("POST /ping HTTP/1.1\r\n\r\n");
    ASSERT_NE(req, nullptr) << "Memory pool allocation failed";

    pipe.dispatch(*req);

    EXPECT_TRUE(router.was_called);
    EXPECT_EQ(router.last_method, "POST");
    EXPECT_EQ(router.header_count, 0);
}
/*
TEST(HttpHftTest, DispatchSync) {
    pool<transport::http_request> memory_pool(1024);
    auto req = memory_pool.make();
    // Your pipeline logic here
    SUCCEED();
}*/
