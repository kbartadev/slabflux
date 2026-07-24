/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * @file json_suite_test.cpp
 * @brief Strict Equivalence Test Harness for Zero-Allocation JSON Producer and Resumable Parser.
 */

#include <gtest/gtest.h>
#include "slabflux/transport/json_producer.hpp"
#include "slabflux/transport/baremetal_json_parser.hpp"
#include <string>
#include <string_view>

using namespace slabflux::transport;

TEST(JsonSuiteTest, RoundTripObjectEmission) {
    alignas(64) char raw_buffer[4096] = {0};
    json_producer builder(raw_buffer, sizeof(raw_buffer));

    // Validate Producer Branchless Logic
    ASSERT_TRUE(builder.begin_object());
    ASSERT_TRUE(builder.add_key("instrument"));
    ASSERT_TRUE(builder.add_string("BTC/USD"));
    ASSERT_TRUE(builder.add_key("price"));
    ASSERT_TRUE(builder.add_number(45000.5));
    ASSERT_TRUE(builder.add_key("flags"));
    ASSERT_TRUE(builder.begin_array());
    ASSERT_TRUE(builder.add_string("margin"));
    ASSERT_TRUE(builder.add_bool(true));
    ASSERT_TRUE(builder.end_array());
    ASSERT_TRUE(builder.add_key("description"));
    ASSERT_TRUE(builder.add_string("Multi-Line-Escape-Test"));
    ASSERT_TRUE(builder.add_key("meta"));
    ASSERT_TRUE(builder.add_null());
    ASSERT_TRUE(builder.end_object());

    std::string_view json = builder.view();

    // Validate Parser Geometry 
    json_token tokens[128];
    json_frame frame;
    frame.tokens = tokens;
    frame.max_tokens = 128;
    frame.reset();

    json_status status = baremetal_json_parser::parse(json, frame, true);
    
    ASSERT_EQ(status, json_status::OK);
    EXPECT_EQ(frame.token_count, 13);
    
    // Root Object mapping check
    EXPECT_EQ(tokens[0].t, json_token::type::OBJECT);
    EXPECT_EQ(tokens[0].size, 5); // 5 keys

    // Array mapping check
    EXPECT_EQ(tokens[5].t, json_token::type::KEY);
    EXPECT_EQ(json.substr(tokens[5].start + 1, tokens[5].length - 2), "flags");
    EXPECT_EQ(tokens[6].t, json_token::type::ARRAY);
    EXPECT_EQ(tokens[6].size, 2); // 2 elements in array
}

TEST(JsonSuiteTest, StreamingResumability) {
    alignas(64) char raw_buffer[4096] = {0};
    json_producer builder(raw_buffer, sizeof(raw_buffer));

    builder.begin_array();
    builder.add_number(1);
    builder.add_number(2);
    builder.add_string("partially_delivered");
    builder.add_bool(false);
    builder.end_array();

    std::string_view complete_json = builder.view();

    json_token tokens[128];
    json_frame frame;
    frame.tokens = tokens;
    frame.max_tokens = 128;
    frame.reset();

    // Feed 1 byte at a time to force massive INCOMPLETE stalls, 
    // testing the pure resilience of the DFA State Machine.
    for (size_t i = 0; i < complete_json.size(); ++i) {
        std::string_view chunk(complete_json.data(), i + 1);
        bool is_eof = (i == complete_json.size() - 1);
        json_status status = baremetal_json_parser::parse(chunk, frame, is_eof);
        
        if (is_eof) {
            ASSERT_EQ(status, json_status::OK);
        } else {
            ASSERT_EQ(status, json_status::INCOMPLETE);
        }
    }
    EXPECT_EQ(frame.token_count, 5); // ARRAY, NUM, NUM, STR, BOOL_FALSE
}