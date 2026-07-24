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
#include <immintrin.h>
#include <string_view>
#include <vector>
#include "slabflux/hw/intrinsics.hpp"

struct mock_header {
    std::string_view name;
    std::string_view value;
};

std::vector<mock_header> parse_headers_avx2(const char* payload_ptr) {
    std::vector<mock_header> headers;

    const __m256i v_newline = _mm256_set1_epi8('\n');
    const __m256i v_colon = _mm256_set1_epi8(':');

    uint32_t line_start = 0;
    uint32_t colon_pos = 0;
    bool has_colon = false;

    // Fixed iteration: read up to 128 bytes of payload (jumps of 32 bytes)
    for (uint32_t offset = 0; offset < 128; offset += 32) {
        __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(payload_ptr + offset));
        uint32_t nl_mask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, v_newline));
        uint32_t colon_mask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, v_colon));

        // Process ALL newlines found in this 32‑byte block
        while (nl_mask) {
            uint32_t nl_idx = slabflux::hw::tzcnt_32(nl_mask);
            uint32_t absolute_nl = offset + nl_idx;

            // Empty line (\r\n) -> HTTP header section ended!
            if (absolute_nl == line_start ||
                (absolute_nl == line_start + 1 && payload_ptr[line_start] == '\r')) {
                return headers;
            }

            // Search for colon (only bits BEFORE the newline)
            uint32_t chunk_line_mask = (1ULL << nl_idx) - 1;
            uint32_t valid_colons = colon_mask & chunk_line_mask;

            uint32_t final_colon_pos = 0;

            // THE MAGIC: Cross‑boundary memory!
            if (has_colon) {
                // Colon was already found in a PREVIOUS 32‑byte block!
                final_colon_pos = colon_pos;
            }
            else if (valid_colons) {
                // Colon is in THIS block
                final_colon_pos = offset + slabflux::hw::tzcnt_32(valid_colons);
            }

            if (final_colon_pos > line_start) {
                headers.push_back({
                    std::string_view(payload_ptr + line_start, final_colon_pos - line_start),
                    std::string_view(payload_ptr + final_colon_pos + 2, absolute_nl - final_colon_pos - 3)
                    });
            }

            line_start = absolute_nl + 1;
            has_colon = false;

            // Clear processed newline and colons before it from the mask
            nl_mask &= (nl_mask - 1);
            colon_mask &= ~chunk_line_mask;
        }

        // If a colon remains in the block (but no newline), save it for NEXT iteration!
        if (colon_mask && !has_colon) {
            colon_pos = offset + slabflux::hw::tzcnt_32(colon_mask);
            has_colon = true;
        }
    }

    return headers;
}

TEST(Avx2ParserTest, HandlesMultipleColonsCorrectly) {
    // Perfected 128‑byte payload test
    alignas(32) char payload[128] =
        "Host: 127.0.0.1:8080\r\n"
        "Date: Wed, 21 Oct 2015 07:28:00\r\n"
        "X-Custom: a:b:c:d\r\n\r\n";

    auto headers = parse_headers_avx2(payload);

    ASSERT_EQ(headers.size(), 3);

    EXPECT_EQ(headers[0].name, "Host");
    EXPECT_EQ(headers[0].value, "127.0.0.1:8080");

    EXPECT_EQ(headers[1].name, "Date");
    EXPECT_EQ(headers[1].value, "Wed, 21 Oct 2015 07:28:00");

    EXPECT_EQ(headers[2].name, "X-Custom");
    EXPECT_EQ(headers[2].value, "a:b:c:d");
}
