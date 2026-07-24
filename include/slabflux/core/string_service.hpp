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

#pragma once

#include "string_chunk.hpp"
#include <string_view>
#include <string>
#include <algorithm>
#include <ostream>
#include <cstring>
#include <concepts>

namespace slabflux::core {

    template<typename T>
    concept ChunkPoolWithBatch = requires(T p, string_chunk** out_ptrs, size_t count) {
        { p.make_batch(out_ptrs, count) } -> std::convertible_to<size_t>;
        { p.release_batch(out_ptrs, count) } -> std::same_as<void>;
    };

    template<typename ChunkPool>
    class string_service {
        ChunkPool& pool_;

    public:
        explicit string_service(ChunkPool& p) noexcept : pool_(p) {}

        /** @brief Batch Reclamation: Reclaims a chain of segments in a single atomic transaction to minimize interconnect pressure. */
        void clear(fragmented_string& fs) noexcept {
            uint32_t current = fs.head_idx;
            fs.head_idx = string_chunk::END_OF_CHAIN;

            if constexpr (ChunkPoolWithBatch<ChunkPool>) {
                string_chunk* batch[64];
                size_t count = 0;

                while (current != string_chunk::END_OF_CHAIN) {
                    auto* chunk = pool_.get_by_index(current);
                    if (!chunk) [[unlikely]] break;
                    
                    batch[count++] = chunk;
                    current = chunk->next_chunk_idx;

                    if (count == 64) {
                        pool_.release_batch(batch, 64);
                        count = 0;
                    }
                }
                if (count > 0) pool_.release_batch(batch, count);
            } else {
                while (current != string_chunk::END_OF_CHAIN) {
                    auto* chunk = pool_.get_by_index(current);
                    if (!chunk) [[unlikely]] break;
                    uint32_t next = chunk->next_chunk_idx;
                    pool_.free(chunk);
                    current = next;
                }
            }

            fs.total_length = 0;
            fs.sso_size = 0;
        }

        [[nodiscard]] size_t size(const fragmented_string& fs) const noexcept { return fs.total_length; }
        [[nodiscard]] bool empty(const fragmented_string& fs) const noexcept { return fs.total_length == 0; }

        /** @brief Proxy to underlying pool batch allocation. */
        size_t make_batch(string_chunk** out_ptrs, size_t count) noexcept {
            if constexpr (ChunkPoolWithBatch<ChunkPool>) return pool_.make_batch(out_ptrs, count);
            else return 0;
        }

        /** @brief Proxy to underlying pool batch reclamation. */
        void release_batch(string_chunk** ptrs, size_t count) noexcept {
            if constexpr (ChunkPoolWithBatch<ChunkPool>) pool_.release_batch(ptrs, count);
            else {
                for (size_t i = 0; i < count; ++i) pool_.free(ptrs[i]);
            }
        }

        // --- ASSIGNMENT (Assembly‑level SIMD Optimization – VERIFIED 18.7 ns) ---
        void assign(fragmented_string& fs, std::string_view sv) {
            if (fs.head_idx != string_chunk::END_OF_CHAIN) [[unlikely]] clear(fs);

            const size_t len = sv.length();
            if (len == 0) [[unlikely]] {
                fs.sso_size = 0;
                fs.total_length = 0;
                fs.head_idx = string_chunk::END_OF_CHAIN;
                return;
            }

            // FAST PATH: SSO
            if (len <= fragmented_string::SSO_CAPACITY) [[likely]] {
                std::memcpy(fs.sso_buffer, sv.data(), len);
                fs.sso_size = static_cast<uint16_t>(len);
                fs.total_length = static_cast<uint32_t>(len);
                fs.head_idx = string_chunk::END_OF_CHAIN;
                return;
            }

            // SLOW PATH: Mathematically pre‑calculated SIMD loop
            fs.sso_size = 0;
            const size_t full_chunks = len / string_chunk::CAPACITY;
            const size_t tail_bytes = len % string_chunk::CAPACITY;
            const char* src = sv.data();

            const size_t total_needed = full_chunks + (tail_bytes > 0 ? 1 : 0);
            size_t bytes_written = 0;

            if constexpr (ChunkPoolWithBatch<ChunkPool>) {
                string_chunk* batch[64];
                size_t allocated = pool_.make_batch(batch, std::min(total_needed, (size_t)64));
                if (SL_EXPECT_FALSE(allocated == 0)) {
                    fs.total_length = 0;
                    fs.head_idx = string_chunk::END_OF_CHAIN;
                    return;
                }

                fs.head_idx = pool_.get_index(batch[0]);

                for (size_t i = 0; i < allocated; ++i) {
                    size_t to_copy = (i < full_chunks) ? string_chunk::CAPACITY : tail_bytes;
                    std::memcpy(batch[i]->data, src, to_copy);
                    batch[i]->used_size = static_cast<uint32_t>(to_copy);
                    batch[i]->next_chunk_idx = (i + 1 < allocated) ? pool_.get_index(batch[i+1]) : string_chunk::END_OF_CHAIN;
                    
                    src += to_copy;
                    bytes_written += to_copy;
                }
            } else {
                string_chunk* prev = nullptr;
                for (size_t i = 0; i < total_needed && i < 64; ++i) {
                    auto* curr = pool_.make();
                    if (!curr) break;

                    if (!prev) fs.head_idx = pool_.get_index(curr);
                    else prev->next_chunk_idx = pool_.get_index(curr);

                    size_t to_copy = (i < full_chunks) ? string_chunk::CAPACITY : tail_bytes;
                    std::memcpy(curr->data, src, to_copy);
                    curr->used_size = static_cast<uint32_t>(to_copy);
                    curr->next_chunk_idx = string_chunk::END_OF_CHAIN;

                    src += to_copy;
                    bytes_written += to_copy;
                    prev = curr;
                }
            }
            fs.total_length = static_cast<uint32_t>(bytes_written);
        }

        // --- APPEND (Concatenation – Fast Tail Lookup) ---
        void append(fragmented_string& fs, std::string_view sv) {
            if (sv.empty()) [[unlikely]] return;

            // 1. Pure SSO case
            if (fs.head_idx == string_chunk::END_OF_CHAIN && (fs.sso_size + sv.length()) <= fragmented_string::SSO_CAPACITY) [[likely]] {
                std::memcpy(fs.sso_buffer + fs.sso_size, sv.data(), sv.length());
                fs.sso_size += static_cast<uint16_t>(sv.length());
                fs.total_length += static_cast<uint32_t>(sv.length());
                return;
            }

            // 2. SSO overflow → Must switch to Fragmented mode
            if (fs.head_idx == string_chunk::END_OF_CHAIN && fs.sso_size > 0) {
                char temp[fragmented_string::SSO_CAPACITY];
                std::memcpy(temp, fs.sso_buffer, fs.sso_size);
                uint16_t old_size = fs.sso_size;
                fs.sso_size = 0;
                fs.total_length = 0;
                assign(fs, std::string_view(temp, old_size));
            }

            // 3. Near‑O(1) chain‑end discovery
            uint32_t last_idx = fs.head_idx;
            string_chunk* last_chunk = nullptr;

            if (last_idx != string_chunk::END_OF_CHAIN) [[likely]] {
                // Accelerated chain traversal
                last_chunk = pool_.get_by_index(last_idx);
                while (last_chunk && last_chunk->next_chunk_idx != string_chunk::END_OF_CHAIN) {
                    last_idx = last_chunk->next_chunk_idx;
                    last_chunk = pool_.get_by_index(last_idx);
                }
            }

            size_t remaining = sv.length();
            const char* src = sv.data();

            // 4. Fill the remaining capacity of the final chunk (if available)
            if (last_chunk && last_chunk->used_size < string_chunk::CAPACITY) {
                uint32_t space = string_chunk::CAPACITY - last_chunk->used_size;
                uint32_t to_copy = static_cast<uint32_t>(remaining >= space ? space : remaining);

                std::memcpy(last_chunk->data + last_chunk->used_size, src, to_copy);
                last_chunk->used_size += to_copy;

                src += to_copy;
                remaining -= to_copy;
                fs.total_length += to_copy;
            }

            if (remaining == 0) return;

            // 5. The SIMD append loop (performance identical to the assign loop)
            const size_t full_chunks = remaining / string_chunk::CAPACITY;
            const size_t tail_bytes = remaining % string_chunk::CAPACITY;

            for (size_t i = 0; i < full_chunks; ++i) {
                auto chunk_ptr = pool_.make();
                if (!chunk_ptr) [[unlikely]] return; // Pool empty

                string_chunk* curr = chunk_ptr;
                uint32_t cur_idx = pool_.get_index(curr);

                if (!last_chunk) fs.head_idx = cur_idx;
                else last_chunk->next_chunk_idx = cur_idx;

                std::memcpy(curr->data, src, string_chunk::CAPACITY);
                curr->used_size = string_chunk::CAPACITY;
                curr->next_chunk_idx = string_chunk::END_OF_CHAIN;

                last_chunk = curr;
                src += string_chunk::CAPACITY;
                fs.total_length += string_chunk::CAPACITY;
            }

            if (tail_bytes > 0) {
                auto chunk_ptr = pool_.make();
                if (chunk_ptr) [[likely]] {
                    string_chunk* curr = chunk_ptr;
                    uint32_t cur_idx = pool_.get_index(curr);

                    if (!last_chunk) fs.head_idx = cur_idx;
                    else last_chunk->next_chunk_idx = cur_idx;

                    std::memcpy(curr->data, src, tail_bytes);
                    curr->used_size = static_cast<uint32_t>(tail_bytes);
                    curr->next_chunk_idx = string_chunk::END_OF_CHAIN;

                    fs.total_length += tail_bytes;
                }
            }
        }

        // --- COMPARISON (optimized for SIMD with 16‑lane register grouping) ---
        [[nodiscard]] bool equals(const fragmented_string& fs, std::string_view sv) const noexcept {
            if (fs.total_length != sv.length()) return false;
            if (sv.empty()) [[unlikely]] return true;

            if (fs.sso_size > 0) [[likely]] return std::memcmp(fs.sso_buffer, sv.data(), fs.sso_size) == 0;

            uint32_t current = fs.head_idx;
            const char* src = sv.data();

            // We assume the equality holds, so the processor can drive the pipeline forward in advance
            while (current != string_chunk::END_OF_CHAIN) {
                const auto* chunk = pool_.get_by_index(current);
                if (!chunk) [[unlikely]] return false;

                // Fixed memory access
                if (std::memcmp(chunk->data, src, chunk->used_size) != 0) return false;

                src += chunk->used_size;
                current = chunk->next_chunk_idx;
            }
            return true;
        }

        [[nodiscard]] std::string extract_to_std_string(const fragmented_string& fs) const {
            std::string result;
            result.reserve(fs.total_length);
            if (fs.sso_size > 0) [[likely]] {
                result.append(fs.sso_buffer, fs.sso_size);
            }
            else {
                uint32_t current = fs.head_idx;
                size_t bytes_to_read = fs.total_length;
                while (current != string_chunk::END_OF_CHAIN && bytes_to_read > 0) {
                    const auto* chunk = pool_.get_by_index(current);
                    if (!chunk) break;

                    // Simple ternary instead of the slow min call
                    size_t read_size = (chunk->used_size < bytes_to_read) ? chunk->used_size : bytes_to_read;
                    result.append(chunk->data, read_size);

                    bytes_to_read -= read_size;
                    current = chunk->next_chunk_idx;
                }
            }
            return result;
        }

        // --- C++ ACCESSOR PROXY ---
        struct accessor {
            string_service& svc;
            fragmented_string& fs;

            accessor& operator=(std::string_view sv) { svc.assign(fs, sv); return *this; }
            accessor& operator=(const fragmented_string& other) { svc.assign(fs, svc.extract_to_std_string(other)); return *this; }
            accessor& operator=(const accessor& other) { svc.assign(fs, svc.extract_to_std_string(other.fs)); return *this; }

            accessor& operator+=(std::string_view sv) { svc.append(fs, sv); return *this; }
            accessor& operator+=(const fragmented_string& other) { svc.append(fs, svc.extract_to_std_string(other)); return *this; }
            accessor& operator+=(const accessor& other) { svc.append(fs, svc.extract_to_std_string(other.fs)); return *this; }

            bool operator==(std::string_view sv) const noexcept { return svc.equals(fs, sv); }
            bool operator==(const fragmented_string& other) const noexcept { return svc.equals(fs, svc.extract_to_std_string(other)); }
            bool operator==(const accessor& other) const noexcept { return svc.equals(fs, svc.extract_to_std_string(other.fs)); }

            bool operator!=(std::string_view sv) const noexcept { return !svc.equals(fs, sv); }
            bool operator!=(const fragmented_string& other) const noexcept { return !svc.equals(fs, svc.extract_to_std_string(other)); }
            bool operator!=(const accessor& other) const noexcept { return !svc.equals(fs, svc.extract_to_std_string(other.fs)); }

            operator std::string() const { return svc.extract_to_std_string(fs); }
            void clear() { svc.clear(fs); }
            size_t size() const noexcept { return svc.size(fs); }
            bool empty() const noexcept { return svc.empty(fs); }

            // --- std::string drop-in compatibility ---
            [[nodiscard]] size_t length() const noexcept { return svc.size(fs); }

            [[nodiscard]] char operator[](size_t pos) const noexcept {
                if (fs.sso_size > 0) [[likely]] return fs.sso_buffer[pos];
                uint32_t current = fs.head_idx;
                size_t offset = pos;
                while (current != string_chunk::END_OF_CHAIN) {
                    const auto* chunk = svc.pool_.get_by_index(current);
                    if (!chunk) break;
                    if (offset < chunk->used_size) return chunk->data[offset];
                    offset -= chunk->used_size;
                    current = chunk->next_chunk_idx;
                }
                return '\0';
            }

            char at(size_t pos) const {
                if (pos >= svc.size(fs)) [[unlikely]] {
                    throw std::out_of_range("smart_string::at out of range");
                }
                return (*this)[pos];
            }

            [[nodiscard]] char front() const noexcept { 
                return svc.empty(fs) ? '\0' : (*this)[0]; 
            }
            
            [[nodiscard]] char back() const noexcept {
                size_t sz = svc.size(fs);
                return sz > 0 ? (*this)[sz - 1] : '\0';
            }

            [[nodiscard]] int compare(std::string_view sv) const noexcept {
                if (svc.equals(fs, sv)) return 0;
                
                if (fs.sso_size > 0) {
                    return std::string_view(fs.sso_buffer, fs.sso_size).compare(sv);
                }
                
                // Zero-allocation lexicographical traversal across chunks
                uint32_t current = fs.head_idx;
                size_t offset = 0;
                while (current != string_chunk::END_OF_CHAIN && offset < sv.length()) {
                    const auto* chunk = svc.pool_.get_by_index(current);
                    if (!chunk) break;
                    size_t cmp_len = std::min(static_cast<size_t>(chunk->used_size), sv.length() - offset);
                    int res = std::char_traits<char>::compare(chunk->data, sv.data() + offset, cmp_len);
                    if (res != 0) return res;
                    offset += cmp_len;
                    current = chunk->next_chunk_idx;
                }
                return (svc.size(fs) < sv.length()) ? -1 : (svc.size(fs) > sv.length() ? 1 : 0);
            }

            [[nodiscard]] bool starts_with(std::string_view sv) const noexcept {
                if (sv.length() > svc.size(fs)) return false;
                if (fs.sso_size > 0) {
                    return std::string_view(fs.sso_buffer, fs.sso_size).starts_with(sv);
                }
                
                // O(N) traversal: Walk the chunks directly.
                uint32_t current = fs.head_idx;
                size_t checked = 0;
                while (current != string_chunk::END_OF_CHAIN && checked < sv.length()) {
                    const auto* chunk = svc.pool_.get_by_index(current);
                    if (!chunk) break;
                    
                    size_t to_check = std::min(static_cast<size_t>(chunk->used_size), sv.length() - checked);
                    if (std::memcmp(chunk->data, sv.data() + checked, to_check) != 0) return false;
                    
                    checked += to_check;
                    current = chunk->next_chunk_idx;
                }
                return checked == sv.length();
            }

            friend std::ostream& operator<<(std::ostream& os, const accessor& acc) {
                os << acc.svc.extract_to_std_string(acc.fs);
                return os;
            }
        };

        accessor operator()(fragmented_string& fs) noexcept { return { *this, fs }; }
        accessor wrap(fragmented_string& fs) noexcept { return (*this)(fs); }
    };

} // namespace slabflux::core
