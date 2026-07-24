/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * @file baremetal_json_parser.hpp
 * @brief Resumable, Zero-Allocation JSON Lexer using Computed Gotos.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>
#include "slabflux/core.hpp"
#include "slabflux/transport/json_simd_utils.hpp"

namespace slabflux::transport {

    struct json_token {
        enum class type : uint8_t { OBJECT, ARRAY, STRING, NUMBER, BOOL_TRUE, BOOL_FALSE, NULL_VAL, KEY };
        type t;
        uint32_t start;
        uint32_t length;
        uint32_t size; // Children count for OBJECT/ARRAY
        int32_t parent;
    };

    enum class json_parser_state : uint8_t {
        ROOT_VALUE,
        OBJ_KEY_OR_END,
        OBJ_KEY,
        OBJ_COLON,
        OBJ_VALUE,
        OBJ_COMMA_OR_END,
        ARR_VALUE_OR_END,
        ARR_VALUE,
        ARR_COMMA_OR_END,
        IN_STRING,
        IN_NUMBER,
        IN_TRUE, IN_FALSE, IN_NULL,
        DONE
    };

    enum class json_status { OK, INCOMPLETE, ERROR };

    struct json_frame {
        json_token* tokens;
        size_t max_tokens;
        size_t token_count{0};
        
        json_parser_state state{json_parser_state::ROOT_VALUE};
        json_parser_state ret_state{json_parser_state::DONE};
        
        size_t total_bytes_consumed{0};
        size_t token_start{0};
        int32_t current_parent{-1};
        uint8_t literal_idx{0};
        bool escape_pending{false};

        void reset() {
            token_count = 0;
            state = json_parser_state::ROOT_VALUE;
            ret_state = json_parser_state::DONE;
            total_bytes_consumed = 0;
            token_start = 0;
            current_parent = -1;
            literal_idx = 0;
            escape_pending = false;
        }
    };

    class baremetal_json_parser {
    private:
        static SLAB_FORCE_INLINE size_t skip_whitespace(const char* buf, size_t len, size_t pos) noexcept {
            while (pos < len) {
                unsigned char c = buf[pos];
                if (SL_LIKELY(c > 32)) return pos;
                if (c == ' ' || c == '\n' || c == '\r' || c == '\t') pos++;
                else return pos; 
            }
            return pos;
        }

    public:
        static json_status parse(std::string_view buffer, json_frame& frame, bool is_eof = false) noexcept {
            if (buffer.empty() && !is_eof) return json_status::INCOMPLETE;
            if (frame.total_bytes_consumed > buffer.size()) frame.reset(); // Rebase

            static const void* const dispatch[] = {
                &&L_ROOT_VALUE, &&L_OBJ_KEY_OR_END, &&L_OBJ_KEY, &&L_OBJ_COLON,
                &&L_OBJ_VALUE, &&L_OBJ_COMMA_OR_END, &&L_ARR_VALUE_OR_END,
                &&L_ARR_VALUE, &&L_ARR_COMMA_OR_END, &&L_IN_STRING,
                &&L_IN_NUMBER, &&L_IN_TRUE, &&L_IN_FALSE, &&L_IN_NULL, &&L_DONE
            };

            size_t pos = frame.total_bytes_consumed;
            size_t len = buffer.size();
            const char* buf = buffer.data();
            unsigned char c;

            auto emit_token = [&](json_token::type t, size_t start, size_t length) -> bool {
                if (SL_EXPECT_FALSE(frame.token_count >= frame.max_tokens)) return false;
                frame.tokens[frame.token_count] = json_token{t, static_cast<uint32_t>(start), static_cast<uint32_t>(length), 0, frame.current_parent};
                
                if (frame.current_parent != -1) {
                    auto& p = frame.tokens[frame.current_parent];
                    // In an ARRAY, every value is a child. In an OBJECT, members are counted in L_OBJ_COLON.
                    if (p.t == json_token::type::ARRAY) {
                        p.size++;
                    }
                }
                frame.token_count++;
                return true;
            };

            auto enter_value = [&](json_parser_state next_state) -> bool {
                if (c == '{') {
                    if (!emit_token(json_token::type::OBJECT, pos, 0)) return false;
                    frame.current_parent = frame.token_count - 1;
                    frame.state = json_parser_state::OBJ_KEY_OR_END; pos++; return true;
                } else if (c == '[') {
                    if (!emit_token(json_token::type::ARRAY, pos, 0)) return false;
                    frame.current_parent = frame.token_count - 1;
                    frame.state = json_parser_state::ARR_VALUE_OR_END; pos++; return true;
                } else if (c == '"') {
                    frame.token_start = pos; frame.ret_state = next_state;
                    frame.state = json_parser_state::IN_STRING; pos++; return true;
                } else if (c == 't') {
                    frame.token_start = pos; frame.ret_state = next_state; frame.literal_idx = 1;
                    frame.state = json_parser_state::IN_TRUE; pos++; return true;
                } else if (c == 'f') {
                    frame.token_start = pos; frame.ret_state = next_state; frame.literal_idx = 1;
                    frame.state = json_parser_state::IN_FALSE; pos++; return true;
                } else if (c == 'n') {
                    frame.token_start = pos; frame.ret_state = next_state; frame.literal_idx = 1;
                    frame.state = json_parser_state::IN_NULL; pos++; return true;
                } else if ((c >= '0' && c <= '9') || c == '-') {
                    frame.token_start = pos; frame.ret_state = next_state;
                    frame.state = json_parser_state::IN_NUMBER; pos++; return true;
                }
                return false;
            };

            goto *dispatch[(uint8_t)frame.state];

L_ROOT_VALUE:
            pos = skip_whitespace(buf, len, pos);
            if (pos == len) goto L_INCOMPLETE;
            c = buf[pos];
            if (!enter_value(json_parser_state::DONE)) return json_status::ERROR;
            goto *dispatch[(uint8_t)frame.state];

L_OBJ_KEY_OR_END:
            pos = skip_whitespace(buf, len, pos);
            if (pos == len) goto L_INCOMPLETE;
            c = buf[pos];
            if (c == '}') {
                frame.tokens[frame.current_parent].length = (pos + 1) - frame.tokens[frame.current_parent].start;
                frame.current_parent = frame.tokens[frame.current_parent].parent;
                pos++; 
                if (frame.current_parent == -1) {
                    frame.state = json_parser_state::DONE;
                } else {
                    auto& p = frame.tokens[frame.current_parent];
                    frame.state = (p.t == json_token::type::OBJECT) ? json_parser_state::OBJ_COMMA_OR_END : json_parser_state::ARR_COMMA_OR_END;
                }
                goto *dispatch[(uint8_t)frame.state];
            }
            if (c == '"') {
                frame.token_start = pos; frame.ret_state = json_parser_state::OBJ_COLON;
                frame.state = json_parser_state::IN_STRING; pos++; goto L_IN_STRING;
            }
            return json_status::ERROR;

L_OBJ_KEY:
            pos = skip_whitespace(buf, len, pos);
            if (pos == len) goto L_INCOMPLETE;
            if (buf[pos] == '"') {
                frame.token_start = pos; frame.ret_state = json_parser_state::OBJ_COLON;
                frame.state = json_parser_state::IN_STRING; pos++; goto L_IN_STRING;
            }
            return json_status::ERROR;

L_OBJ_COLON:
            pos = skip_whitespace(buf, len, pos);
            if (pos == len) goto L_INCOMPLETE;
            if (buf[pos] != ':') return json_status::ERROR;
            pos++; frame.state = json_parser_state::OBJ_VALUE;
            // Change the last STRING token type to KEY
            frame.tokens[frame.token_count - 1].t = json_token::type::KEY;
            // Increment the parent object size for the established key-value pair
            frame.tokens[frame.current_parent].size++;
            goto L_OBJ_VALUE;

L_OBJ_VALUE:
            pos = skip_whitespace(buf, len, pos);
            if (pos == len) goto L_INCOMPLETE;
            c = buf[pos];
            if (!enter_value(json_parser_state::OBJ_COMMA_OR_END)) return json_status::ERROR;
            goto *dispatch[(uint8_t)frame.state];

L_OBJ_COMMA_OR_END:
            pos = skip_whitespace(buf, len, pos);
            if (pos == len) goto L_INCOMPLETE;
            c = buf[pos];
            if (c == ',') { frame.state = json_parser_state::OBJ_KEY; pos++; goto L_OBJ_KEY; }
            if (c == '}') {
                frame.tokens[frame.current_parent].length = (pos + 1) - frame.tokens[frame.current_parent].start;
                frame.current_parent = frame.tokens[frame.current_parent].parent;
                pos++; 
                if (frame.current_parent == -1) {
                    frame.state = json_parser_state::DONE;
                } else {
                    auto& p = frame.tokens[frame.current_parent];
                    frame.state = (p.t == json_token::type::OBJECT) ? json_parser_state::OBJ_COMMA_OR_END : json_parser_state::ARR_COMMA_OR_END;
                }
                goto *dispatch[(uint8_t)frame.state];
            }
            return json_status::ERROR;

L_ARR_VALUE_OR_END:
            pos = skip_whitespace(buf, len, pos);
            if (pos == len) goto L_INCOMPLETE;
            c = buf[pos];
            if (c == ']') {
                frame.tokens[frame.current_parent].length = (pos + 1) - frame.tokens[frame.current_parent].start;
                frame.current_parent = frame.tokens[frame.current_parent].parent;
                pos++; 
                if (frame.current_parent == -1) {
                    frame.state = json_parser_state::DONE;
                } else {
                    auto& p = frame.tokens[frame.current_parent];
                    frame.state = (p.t == json_token::type::OBJECT) ? json_parser_state::OBJ_COMMA_OR_END : json_parser_state::ARR_COMMA_OR_END;
                }
                goto *dispatch[(uint8_t)frame.state];
            }
            goto L_ARR_VALUE;

L_ARR_VALUE:
            pos = skip_whitespace(buf, len, pos);
            if (pos == len) goto L_INCOMPLETE;
            c = buf[pos];
            if (!enter_value(json_parser_state::ARR_COMMA_OR_END)) return json_status::ERROR;
            goto *dispatch[(uint8_t)frame.state];

L_ARR_COMMA_OR_END:
            pos = skip_whitespace(buf, len, pos);
            if (pos == len) goto L_INCOMPLETE;
            c = buf[pos];
            if (c == ',') { frame.state = json_parser_state::ARR_VALUE; pos++; goto L_ARR_VALUE; }
            if (c == ']') {
                frame.tokens[frame.current_parent].length = (pos + 1) - frame.tokens[frame.current_parent].start;
                frame.current_parent = frame.tokens[frame.current_parent].parent;
                pos++; 
                if (frame.current_parent == -1) {
                    frame.state = json_parser_state::DONE;
                } else {
                    auto& p = frame.tokens[frame.current_parent];
                    frame.state = (p.t == json_token::type::OBJECT) ? json_parser_state::OBJ_COMMA_OR_END : json_parser_state::ARR_COMMA_OR_END;
                }
                goto *dispatch[(uint8_t)frame.state];
            }
            return json_status::ERROR;

L_IN_STRING:
            if (frame.escape_pending) { frame.escape_pending = false; pos++; if (pos >= len) goto L_INCOMPLETE; }
            while (true) {
                size_t d = json::find_string_delimiter(buf, len, pos);
                if (d == std::string_view::npos) { pos = len; goto L_INCOMPLETE; }
                c = buf[d];
                if (c == '"') {
                    // The emitted token's length should be the raw length including escapes.
                    // The actual string value would need a separate decoding step if the test
                    // expects the *decoded* string. For zero-copy, we emit the raw view.
                    if (!emit_token(json_token::type::STRING, frame.token_start, d - frame.token_start + 1)) return json_status::ERROR; // +1 for the closing quote
                    pos = d + 1; frame.state = frame.ret_state; goto *dispatch[(uint8_t)frame.state];
                } else if (c == '\\') {
                    if (d + 1 >= len) { frame.escape_pending = true; pos = len; goto L_INCOMPLETE; }
                    char next_char = buf[d + 1];
                    if (next_char == 'u') {
                        // Unicode escape \uXXXX. Need 4 hex digits.
                        if (d + 5 >= len) { frame.escape_pending = true; pos = len; goto L_INCOMPLETE; }
                        // For a zero-copy parser, we just skip the escape sequence.
                        // A full JSON parser would decode this into a character.
                        pos = d + 6; // Skip '\uXXXX'
                    } else if (next_char == '"' || next_char == '\\' || next_char == '/' ||
                               next_char == 'b' || next_char == 'f' || next_char == 'n' ||
                               next_char == 'r' || next_char == 't') {
                        pos = d + 2; // Skip '\"', '\\', '\/', '\b', '\f', '\n', '\r', '\t'
                    } else {
                        return json_status::ERROR; // Invalid escape sequence
                    }
                } else {
                    // This 'else' is reached if `c` is a delimiter but not `"` or `\`.
                    // `json::find_string_delimiter` finds `"` `\` or `c < 32`.
                    // So if `c` is not `"` or `\`, it must be `c < 32` (an unescaped control character).
                    return json_status::ERROR; // Unescaped control character
                }
            }

L_IN_NUMBER:
            while (pos < len) {
                c = buf[pos];
                if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') pos++;
                else break;
            }
            if (pos == len && !is_eof) goto L_INCOMPLETE;
            if (!emit_token(json_token::type::NUMBER, frame.token_start, pos - frame.token_start)) return json_status::ERROR;
            frame.state = frame.ret_state; goto *dispatch[(uint8_t)frame.state];

L_IN_TRUE:
            { static const char expected[] = "true";
            while (pos < len) {
                if (buf[pos] != expected[frame.literal_idx]) return json_status::ERROR;
                pos++; frame.literal_idx++;
                if (frame.literal_idx == 4) {
                    if (!emit_token(json_token::type::BOOL_TRUE, frame.token_start, 4)) return json_status::ERROR;
                    frame.state = frame.ret_state; goto *dispatch[(uint8_t)frame.state];
                }
            } goto L_INCOMPLETE; }
L_IN_FALSE:
            { static const char expected[] = "false";
            while (pos < len) { if (buf[pos] != expected[frame.literal_idx]) return json_status::ERROR; pos++; frame.literal_idx++; if (frame.literal_idx == 5) { if (!emit_token(json_token::type::BOOL_FALSE, frame.token_start, 5)) return json_status::ERROR; frame.state = frame.ret_state; goto *dispatch[(uint8_t)frame.state]; } } goto L_INCOMPLETE; }
L_IN_NULL:
            { static const char expected[] = "null";
            while (pos < len) { if (buf[pos] != expected[frame.literal_idx]) return json_status::ERROR; pos++; frame.literal_idx++; if (frame.literal_idx == 4) { if (!emit_token(json_token::type::NULL_VAL, frame.token_start, 4)) return json_status::ERROR; frame.state = frame.ret_state; goto *dispatch[(uint8_t)frame.state]; } } goto L_INCOMPLETE; }

L_DONE:
            frame.total_bytes_consumed = pos;
            return json_status::OK;

L_INCOMPLETE:
            frame.total_bytes_consumed = pos;
            return json_status::INCOMPLETE;
        }
    };

} // namespace slabflux::transport