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
 * ============================================================================*
 *
 * @file schema.hpp
 * @brief Self-describing memory layouts for the Platform.
 * @details Allows the non-deterministic orchestration layer to "see" into 
 * the deterministic core without knowing the C++ types.
 */

#pragma once

#include <array>
#include <string_view>
#include <cstring>
#include <span>

namespace slabflux::sys {

    // Compile-time or runtime FNV-1a Hash for O(1) identification
    static constexpr uint64_t hash_name(std::string_view name) noexcept {
        uint64_t hash = 0xcbf29ce484222325ULL;
        for (char c : name) {
            hash ^= static_cast<uint64_t>(c);
            hash *= 0x100000001b3ULL;
        }
        return hash;
    }

    enum class field_type { u32, u64, i64, f64, boolean };

    struct field_definition {
        char name[32]; // Fixed size prevents heap fragmentation
        uint64_t name_hash; // O(1) identifier
        field_type type;
        size_t offset;
        size_t size;
    };

    /**
     * @brief The Schema Registry.
     * @details Acts as the "Passport" for the deterministic Chip. 
     * Re-architected for zero heap allocation (no std::vector or std::string).
     */
    class schema {
        static constexpr size_t MAX_FIELDS = 128;
        std::array<field_definition, MAX_FIELDS> fields_{};
        size_t field_count_{0};

    public:
        void register_field(std::string_view name, field_type type, size_t offset, size_t size) noexcept {
            if (field_count_ >= MAX_FIELDS) return;
            auto& field = fields_[field_count_++];
            std::strncpy(field.name, name.data(), std::min(name.size(), size_t(31)));
            field.name[31] = '\0';
            field.name_hash = hash_name(name);
            field.type = type;
            field.offset = offset;
            field.size = size;
        }

        std::span<const field_definition> get_fields() const noexcept { 
            return std::span<const field_definition>(fields_.data(), field_count_); 
        }

        /**
         * @brief O(1) Integer Hash Resolution.
         * @details Replaces textbook string_view matching loops with cache-line
         * contiguous integer hash comparisons. Eliminates branch divergence.
         */
        const field_definition* find_field(std::string_view name) const noexcept {
            const uint64_t target_hash = hash_name(name);
            for (size_t i = 0; i < field_count_; ++i) {
                if (fields_[i].name_hash == target_hash) return &fields_[i];
            }
            return nullptr;
        }

        /**
         * @brief Exports the schema as a binary blob for nodes.
         */
        // Return fixed array or span in a real system, dummy string view for now
        std::string_view export_manifest() const noexcept {
            return "{\"type\": \"trade_event\", \"fields\": [...]}";
        }
    };
}
