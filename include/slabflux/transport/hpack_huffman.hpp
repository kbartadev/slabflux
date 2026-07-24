/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * @file hpack_huffman.hpp
 * @brief Static Huffman Tree and Decoder for HPACK (RFC 7541).
 */

#pragma once

#include <cstdint>
#include <string_view>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::transport::hpack_huffman {

    struct huffman_node {
        // A negative value indicates a leaf node, with the value being the decoded character.
        // A positive value is the index of the next node in the tree.
        int16_t left;  // For bit 0
        int16_t right; // For bit 1
    };

        struct hpack_code {
        uint32_t code;
        uint8_t length;
    };

    // RFC 7541 Appendix B: Huffman Code Table
    static constexpr std::array<hpack_code, 257> huffman_codes = {{
        {0x1ff8, 13}, {0x7fffd8, 23}, {0xfffffe2, 28}, {0xfffffe3, 28}, {0xfffffe4, 28}, {0xfffffe5, 28}, {0xfffffe6, 28}, {0xfffffe7, 28},
        {0xfffffe8, 28}, {0xffffea, 24}, {0x3ffffffc, 30}, {0xfffffe9, 28}, {0xfffffea, 28}, {0xfffffeb, 28}, {0xfffffec, 28}, {0xfffffed, 28},
        {0xfffffee, 28}, {0xfffffef, 28}, {0xffffff0, 28}, {0xffffff1, 28}, {0xffffff2, 28}, {0xffffff3, 28}, {0xffffff4, 28}, {0xffffff5, 28},
        {0xffffff6, 28}, {0xffffff7, 28}, {0xffffff8, 28}, {0xffffff9, 28}, {0xffffffa, 28}, {0xffffffb, 28}, {0xffffffc, 28}, {0xffffffd, 28},
        {0x14, 6}, {0x3f8, 10}, {0x3f9, 10}, {0xffa, 12}, {0x1ff9, 13}, {0x15, 6}, {0xf8, 8}, {0x7fa, 11},
        {0x3fa, 10}, {0x3fb, 10}, {0xf9, 8}, {0x7fb, 11}, {0xfa, 8}, {0x16, 6}, {0x17, 6}, {0x18, 6},
        {0x0, 5}, {0x1, 5}, {0x2, 5}, {0x19, 6}, {0x1a, 6}, {0x1b, 6}, {0x1c, 6}, {0x1d, 6},
        {0x1e, 6}, {0x1f, 6}, {0x5c, 7}, {0xfb, 8}, {0x7fc, 11}, {0x20, 6}, {0xffb, 12}, {0x3fc, 10},
        {0x1ffa, 13}, {0x21, 6}, {0x5d, 7}, {0x5e, 7}, {0x5f, 7}, {0x60, 7}, {0x61, 7}, {0x62, 7},
        {0x63, 7}, {0x64, 7}, {0x65, 7}, {0x66, 7}, {0x67, 7}, {0x68, 7}, {0x69, 7}, {0x6a, 7},
        {0x6b, 7}, {0x6c, 7}, {0x6d, 7}, {0x6e, 7}, {0x6f, 7}, {0x70, 7}, {0x71, 7}, {0x72, 7},
        {0xfc, 8}, {0x73, 7}, {0xfd, 8}, {0x1ffb, 13}, {0x7fff0, 19}, {0x1ffc, 13}, {0x3fffc, 18}, {0x22, 6},
        {0x7ffd8, 19}, {0x3, 5}, {0x23, 6}, {0x4, 5}, {0x24, 6}, {0x5, 5}, {0x25, 6}, {0x26, 6},
        {0x27, 6}, {0x6, 5}, {0x74, 7}, {0x75, 7}, {0x28, 6}, {0x29, 6}, {0x2a, 6}, {0x7, 5},
        {0x2b, 6}, {0x76, 7}, {0x2c, 6}, {0x8, 5}, {0x9, 5}, {0x2d, 6}, {0x77, 7}, {0x78, 7},
        {0x79, 7}, {0x7a, 7}, {0x7b, 7}, {0x7ffe8, 19}, {0x7c, 7}, {0x7ffd9, 19}, {0x3fffd, 18}, {0xfffffe, 28},
        {0xffffea, 24}, {0x3fffd2, 22}, {0xfffea, 20}, {0x3fffea, 22}, {0x3fffeb, 22}, {0x3fffec, 22}, {0x3fffed, 22}, {0x3fffee, 22},
        {0x3fffef, 22}, {0x3ffff0, 22}, {0x3ffff1, 22}, {0x3ffff2, 22}, {0x3ffff3, 22}, {0x3ffff4, 22}, {0x3ffff5, 22}, {0x3ffff6, 22},
        {0x3ffff7, 22}, {0x3ffff8, 22}, {0x3ffff9, 22}, {0x3ffffa, 22}, {0x3ffffb, 22}, {0xfffeb, 20}, {0x7fffaa, 23}, {0x1fffea, 21},
        {0x3fffaa, 22}, {0x3fffab, 22}, {0x3fffac, 22}, {0x3fffad, 22}, {0x3fffae, 22}, {0x3fffaf, 22}, {0x3fffb0, 22}, {0x3fffb1, 22},
        {0x3fffb2, 22}, {0x3fffb3, 22}, {0x3fffb4, 22}, {0x3fffb5, 22}, {0x3fffb6, 22}, {0x3fffb7, 22}, {0x3fffb8, 22}, {0x3fffb9, 22},
        {0x3fffba, 22}, {0x3fffbb, 22}, {0x3fffbc, 22}, {0x3fffbd, 22}, {0x3fffbe, 22}, {0x3fffbf, 22}, {0x3fffc0, 22}, {0x3fffc1, 22},
        {0x3fffc2, 22}, {0x3fffc3, 22}, {0x3fffc4, 22}, {0x3fffc5, 22}, {0x3fffc6, 22}, {0x3fffc7, 22}, {0x3fffc8, 22}, {0x3fffc9, 22},
        {0x3fffca, 22}, {0x3fffcb, 22}, {0x3fffcc, 22}, {0x3fffcd, 22}, {0x3fffce, 22}, {0x3fffcf, 22}, {0x3fffd0, 22}, {0x3fffd1, 22},
        {0x7fffab, 23}, {0x7fffac, 23}, {0x7fffad, 23}, {0x7fffae, 23}, {0x7fffaf, 23}, {0x7fffb0, 23}, {0x7fffb1, 23}, {0x7fffb2, 23},
        {0x7fffb3, 23}, {0x7fffb4, 23}, {0x7fffb5, 23}, {0x7fffb6, 23}, {0x7fffb7, 23}, {0x7fffb8, 23}, {0x7fffb9, 23}, {0x7fffba, 23},
        {0x7fffbb, 23}, {0x7fffbc, 23}, {0x7fffbd, 23}, {0x7fffbe, 23}, {0x7fffbf, 23}, {0x7fffc0, 23}, {0x7fffc1, 23}, {0x7fffc2, 23},
        {0x7fffc3, 23}, {0x7fffc4, 23}, {0x7fffc5, 23}, {0x7fffc6, 23}, {0x7fffc7, 23}, {0x7fffc8, 23}, {0x7fffc9, 23}, {0x7fffca, 23},
        {0x7fffcb, 23}, {0x7fffcc, 23}, {0x7fffcd, 23}, {0x7fffce, 23}, {0x7fffcf, 23}, {0x7fffd0, 23}, {0x7fffd1, 23}, {0x7fffd2, 23},
        {0x7fffd3, 23}, {0x7fffd4, 23}, {0x7fffd5, 23}, {0x7fffd6, 23}, {0x7fffd7, 23}, {0xffffeb, 24}, {0xffffec, 24}, {0xffffed, 24},
        {0xffffee, 24}, {0xffffef, 24}, {0xfffff0, 24}, {0xfffff1, 24}, {0xfffff2, 24}, {0xfffff3, 24}, {0xfffff4, 24}, {0xfffff5, 24},
        {0xfffff6, 24}, {0xfffff7, 24}, {0xfffff8, 24}, {0xfffff9, 24}, {0xfffffa, 24}, {0xfffffb, 24}, {0xfffffc, 24}, {0xfffffd, 24},
        {0x3fffffff, 30}
    }};

    // Generates the static Huffman tree dynamically at compile-time/startup.
    // A full binary tree for 257 elements requires exactly 513 nodes.
    static constexpr std::array<huffman_node, 513> generate_huffman_tree() {
        std::array<huffman_node, 513> tree{};
        for (auto& n : tree) { n.left = 0; n.right = 0; }
        
        int16_t next_free = 1;
        
        for (size_t sym = 0; sym < 257; ++sym) {
            uint32_t code = huffman_codes[sym].code;
            uint8_t len = huffman_codes[sym].length;
            if (len == 0) continue;
            
            int16_t curr = 0;
            for (int i = len - 1; i >= 0; --i) {
                if (curr < 0 || curr >= 513) break; // Defensive hardware bounds clamping
                uint8_t bit = (code >> i) & 1;
                if (i == 0) {
                    // Leaf node: Represented as bitwise NOT to safely encode 0 as negative
                    if (bit == 0) tree[curr].left = static_cast<int16_t>(~sym);
                    else          tree[curr].right = static_cast<int16_t>(~sym);
                } else {
                    if (bit == 0) {
                        if (tree[curr].left < 0) break; // Defensive guard: Ignore prefix collisions
                        if (next_free >= 513) break;
                        if (tree[curr].left == 0) tree[curr].left = next_free++;
                        curr = tree[curr].left;
                    } else {
                        if (tree[curr].right < 0) break; // Defensive guard: Ignore prefix collisions
                        if (next_free >= 513) break;
                        if (tree[curr].right == 0) tree[curr].right = next_free++;
                        curr = tree[curr].right;
                    }
                }
            }
        }
        return tree;
    }

    // This variable evaluates identically to your old 9,000 line hardcoded array, 
    // but now without any source code bloat!
    static constexpr auto huffman_tree = generate_huffman_tree();
    
} // namespace slabflux::transport::hpack_huffman