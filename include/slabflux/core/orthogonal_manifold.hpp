/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * ============================================================================
 * SLABFLUX SOFTWARE ENGINE - CORE MEMORY SUBSYSTEM
 * Copyright (c) 2026 Kristóf Barta (https://github.com/kbartadev)
 * ============================================================================
 * @file orthogonal_manifold.hpp
 * @brief Orthogonal Dimension-Routed Wait-Free/Lock-Free MPMC Queue.
 */

#pragma once

#include <atomic>
#include <thread>
#include <cstddef>
#include <functional>
#include "slabflux/core/physical_layout.hpp"

namespace slabflux::core {

    template <typename T, std::size_t CapacityLines = 128>
    class orthogonal_manifold {
    private:
        // The matrix width is aligned exactly to one physical Cache Line size.
        // On 64-bit architectures, this typically means 8 pointers per row.
        static constexpr std::size_t COLS = CACHE_LINE_SIZE / sizeof(std::atomic<T*>);
        static constexpr std::size_t ROWS = CapacityLines;

        // Strict L1 cache separation between rows
        struct alignas(CACHE_LINE_SIZE) CacheLineRow {
            std::atomic<T*> slots[COLS];
            
            constexpr CacheLineRow() noexcept {
                for (std::size_t i = 0; i < COLS; ++i) {
                    slots[i].store(nullptr, std::memory_order_relaxed);
                }
            }
        };

        CacheLineRow matrix_[ROWS];

        // Hardware entropy-based seed for uniform thread dispersion across the grid
        static std::size_t thread_seed() noexcept {
            std::size_t h = std::hash<std::thread::id>{}(std::this_thread::get_id());
            h ^= h >> 12; h ^= h << 25; h ^= h >> 27;
            return h;
        }

    public:
        orthogonal_manifold() = default;
        ~orthogonal_manifold() = default;

        // Copying and moving is structurally prohibited
        orthogonal_manifold(const orthogonal_manifold&) = delete;
        orthogonal_manifold& operator=(const orthogonal_manifold&) = delete;

        /**
         * @brief Wait-Free Bounded Producer Inject
         * The thread scans horizontally on its own Cache Line.
         */
        bool push(T* payload) noexcept {
            static thread_local std::size_t tr_row = thread_seed() % ROWS;
            static thread_local std::size_t tr_col = 0;

            for (std::size_t attempt = 0; attempt < ROWS; ++attempt) {
                for (std::size_t c = 0; c < COLS; ++c) {
                    std::size_t col = (tr_col + c) % COLS;
                    T* vacuum = nullptr;
                    
                    if (matrix_[tr_row].slots[col].compare_exchange_strong(
                            vacuum, payload, 
                            std::memory_order_release, 
                            std::memory_order_relaxed)) {
                        tr_col = (col + 1) % COLS; // The next operation starts from here
                        return true;
                    }
                }
                // The row is saturated. Affine jump to the next Cache Line.
                tr_row = (tr_row + 1) % ROWS;
            }
            return false; // The manifold is completely saturated
        }

        /**
         * @brief Lock-Free Bounded Consumer Extract
         * The thread scans vertically at identical offsets, crossing Cache Lines.
         */
        T* pop() noexcept {
            static thread_local std::size_t tc_col = thread_seed() % COLS;
            static thread_local std::size_t tc_row = 0;

            for (std::size_t attempt = 0; attempt < COLS; ++attempt) {
                for (std::size_t r = 0; r < ROWS; ++r) {
                    std::size_t row = (tc_row + r) % ROWS;
                    
                    T* plasma = matrix_[row].slots[tc_col].load(std::memory_order_acquire);
                    if (plasma != nullptr) {
                        if (matrix_[row].slots[tc_col].compare_exchange_strong(
                                plasma, nullptr, 
                                std::memory_order_acquire, 
                                std::memory_order_relaxed)) {
                            tc_row = (row + 1) % ROWS;
                            return plasma;
                        }
                    }
                }
                // The column is completely empty. Jump to the next orthogonal plane.
                tc_col = (tc_col + 1) % COLS;
            }
            return nullptr; // The manifold is completely drained
        }
    };

} // namespace slabflux::core