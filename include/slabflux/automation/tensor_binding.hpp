/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * ============================================================================
 * SLABFLUX SOFTWARE ENGINE
 * Copyright (c) 2026 Kristóf Barta (https://github.com/kbartadev)
 * ============================================================================
 * PROPRIETARY AND SOURCE-AVAILABLE CODEBASE. ALL RIGHTS RESERVED.
 *
 * This source file and all constitutive programmatic expressions contained herein
 * are the exclusive intellectual property of Kristóf Barta, established and
 * distributed strictly under the conditions of the SLABFLUX SOURCE-AVAILABLE
 * AND ECOSYSTEM LICENSE (the "License").
 *
 * TITLE TO AND OWNERSHIP OF THE SOFTWARE, THE ENGINE, CORE LOGIC, ARCHITECTURAL
 * LAYOUTS, AND ALL ASSOCIATED INSIGHTS REMAIN SOLELY VESTED IN THE AUTHOR.
 *
 * ----------------------------------------------------------------------------
 * TECHNICAL WARNING & SYSTEM ARCHITECTURE NOTICE
 * ----------------------------------------------------------------------------
 * THIS SOFTWARE UTILIZES ARCHITECTURE-SPECIFIC HARDWARE INTRINSICS AND OPERATES
 * THROUGH LOW-LEVEL, KERNEL-ADJACENT EXECUTION PATHS THAT REDUCE OR BYPASS STANDARD
 * OPERATING SYSTEM MEDIATION LAYERS. INCORRECT INTEGRATION, EXECUTION, OR CONFIGURATION
 * MAY RESULT IN SEVERE SYSTEM INSTABILITY, KERNEL PANICS, OR PERMANENT LOSS OF DATA,
 * AND MAY RENDER SYSTEMS TEMPORARILY OR PERMANENTLY UNUSABLE UNTIL REPAIRED OR
 * RECONFIGURED.
 *
 * ----------------------------------------------------------------------------
 * ABSOLUTE USAGE RESTRICTIONS & OPERATIONAL PROHIBITIONS
 * ----------------------------------------------------------------------------
 * ANY CORPORATE USE, INSTITUTIONAL INCLUSION (#include), MICRO-ARCHITECTURAL
 * REPLICATION, STRUCTURAL SEQUENCE EXTRACTION, OR CORPORATE DEPLOYMENT IS
 * STRICTLY PROHIBITED AND CONSTITUTES AN IMMEDIATE, WILLFUL INFRINGEMENT
 * OF COPYRIGHT AND CONTRACTUAL BREACH.
 *
 * Execution by individual, independent developers is permitted strictly subject
 * to the conditional grants, mandatory attributions, and structural limitations
 * defined within the License.
 *
 * ----------------------------------------------------------------------------
 * EXPRESS HARDWARE RISK ALLOCATION & DISCLAIMER (UCC CONSPICUOUS NOTICE)
 * ----------------------------------------------------------------------------
 * THE USER EXPRESSLY ACKNOWLEDGES AND AGREES THAT EXECUTION OF THIS SOFTWARE
 * CARRIES AN INHERENT RISK OF TOTAL PHYSICAL HARDWARE FAILURE AND PERMANENT
 * DESTRUCTION OF COMPUTING INFRASTRUCTURE. THE USER VOLUNTARILY ASSUMES ALL
 * SUCH RISKS AS A CONDITION OF EXECUTION TO THE MAXIMUM EXTENT PERMITTED BY LAW.
 * ============================================================================*/

#pragma once

#include <cstdint>
#include <vector>
#include <string_view>
#include <concepts>
#include <type_traits>
#include <stdexcept>
#include <numeric> // For std::accumulate
#include <span>

namespace slabflux::automation {

    /**
     * @brief Enum for common tensor data types.
     * @details Provides a deterministic, platform-independent representation
     * of tensor element types, avoiding `std::type_info` overhead.
     */
    enum class data_type : uint8_t {
        FLOAT32,
        FLOAT64,
        INT8,
        INT16,
        INT32,
        INT64,
        UINT8,
        UINT16,
        UINT32,
        UINT64,
        BOOL,
        UNKNOWN
    };

    /**
     * @brief Concept for a type that structurally represents a tensor.
     * @details Enforces a minimal, explicit interface to ensure bit-perfect
     * with the binding abstraction.
     */
    template <typename T>
    concept Tensor = requires(T t) {
        // Must provide a const pointer to the underlying data.
        { t.data() } -> std::convertible_to<const void*>;
        // Must provide the total number of elements.
        { t.numel() } -> std::convertible_to<size_t>;
        // Must provide the number of dimensions.
        { t.dim() } -> std::convertible_to<size_t>;
        // Must provide a span or vector of dimension sizes.
        { t.shape() } -> std::convertible_to<std::span<const size_t>>;
        // Must provide the data type.
        { t.dtype() } -> std::same_as<data_type>;
        // Must provide the size of a single element in bytes.
        { t.element_size() } -> std::convertible_to<size_t>;
    };

    /**
     * @brief Concept-constrained abstraction for binding to a tensor.
     * @details Provides a safe, compile-time verified interface to interact 
     * with any type satisfying the `tensor` concept.
     *
     * @tparam TensorType A type that satisfies the `tensor` concept.
     */
    template <Tensor TensorType>
    class tensor_binding {
    private:
        TensorType* tensor_ptr_; // Non-owning pointer to the actual tensor

    public:
        /**
         * @brief Constructs a tensor_binding from a reference to a TensorType.
         * @param tensor The tensor to bind to.
         */
        explicit tensor_binding(TensorType& tensor) noexcept : tensor_ptr_(&tensor) {}

        // Disallow copying to prevent accidental deep copies or ownership issues.
        tensor_binding(const tensor_binding&) = delete;
        tensor_binding& operator=(const tensor_binding&) = delete;

        // Allow moving.
        tensor_binding(tensor_binding&&) noexcept = default;
        tensor_binding& operator=(tensor_binding&&) noexcept = default;

        /** @brief Returns a const pointer to the underlying tensor data. */
        [[nodiscard]] const void* data() const noexcept { return tensor_ptr_->data(); }

        /** @brief Returns the total number of elements in the tensor. */
        [[nodiscard]] size_t numel() const noexcept { return tensor_ptr_->numel(); }

        /** @brief Returns the number of dimensions of the tensor. */
        [[nodiscard]] size_t dim() const noexcept { return tensor_ptr_->dim(); }

        /** @brief Returns a span of the tensor's shape (dimension sizes). */
        [[nodiscard]] std::span<const size_t> shape() const noexcept { return tensor_ptr_->shape(); }

        /** @brief Returns the data type of the tensor's elements. */
        [[nodiscard]] data_type dtype() const noexcept { return tensor_ptr_->dtype(); }

        /** @brief Returns the size of a single element in bytes. */
        [[nodiscard]] size_t element_size() const noexcept { return tensor_ptr_->element_size(); }
    };

} // namespace slabflux::automation
