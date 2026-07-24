/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 */

#pragma once

#include <atomic>
#include <thread>
#include <cstddef>
#include <functional>
#include "slabflux/core/physical_layout.hpp"
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::core {

    /**
     * @brief Cross-Orthogonal Grid based MPMC Queue.
     * @details Completely eliminates central head/tail bottlenecks by scattering 
     * threads across a 2D matrix of atomic pointers.
     */
    template <typename T, std::size_t CapacityRows = 128>
    class cross_orthogonal_queue {
    private:
        // Matrix dimensions. Columns are statically bound to fit exactly one cache line.
        static constexpr std::size_t COLS = CACHE_LINE_SIZE / sizeof(std::atomic<T*>);
        static constexpr std::size_t ROWS = CapacityRows;

        // Strict L1 cache isolation between physical rows
        struct alignas(CACHE_LINE_SIZE) cache_line_row {
            std::atomic<T*> slots[COLS];
            
            constexpr cache_line_row() noexcept {
                for (std::size_t i = 0; i < COLS; ++i) {
                    slots[i].store(nullptr, std::memory_order_relaxed);
                }
            }
        };

        cache_line_row grid_[ROWS];

        // Hardware-entropy derived seed for uniform thread dispersion across the grid.
        static std::size_t thread_seed() noexcept {
            std::size_t h = std::hash<std::thread::id>{}(std::this_thread::get_id());
            h ^= h >> 12; h ^= h << 25; h ^= h >> 27;
            return h;
        }

    public:
        cross_orthogonal_queue() = default;
        ~cross_orthogonal_queue() = default;

        cross_orthogonal_queue(const cross_orthogonal_queue&) = delete;
        cross_orthogonal_queue& operator=(const cross_orthogonal_queue&) = delete;

        /**
         * @brief Wait-Free Bounded Push.
         * @details Producers traverse the grid horizontally (row-major), maintaining 
         * high cache-line residency.
         */
        bool push(T* payload) noexcept {
            static thread_local std::size_t p_row = thread_seed() % ROWS;
            static thread_local std::size_t p_col = 0;

            for (std::size_t attempt = 0; attempt < ROWS; ++attempt) {
                for (std::size_t c = 0; c < COLS; ++c) {
                    std::size_t col = (p_col + c) % COLS;
                    T* expected_empty = nullptr;
                    
                    if (grid_[p_row].slots[col].compare_exchange_strong(
                            expected_empty, payload, 
                            std::memory_order_release, 
                            std::memory_order_relaxed)) {
                        p_col = (col + 1) % COLS; // Advance local cursor for next push
                        return true;
                    }
                }
                // Row is fully saturated or highly contested. Jump to the next cache line.
                p_row = (p_row + 1) % ROWS;
            }
            return false; // Queue capacity exhausted
        }

        /**
         * @brief Lock-Free Bounded Pop.
         * @details Consumers traverse the grid vertically (column-major), intersecting 
         * with producers at precisely one cell per row.
         */
        T* pop() noexcept {
            static thread_local std::size_t c_col = thread_seed() % COLS;
            static thread_local std::size_t c_row = 0;

            for (std::size_t attempt = 0; attempt < COLS; ++attempt) {
                for (std::size_t r = 0; r < ROWS; ++r) {
                    std::size_t row = (c_row + r) % ROWS;
                    
                    T* item = grid_[row].slots[c_col].load(std::memory_order_acquire);
                    if (item != nullptr) {
                        if (grid_[row].slots[c_col].compare_exchange_strong(
                                item, nullptr, 
                                std::memory_order_acquire, 
                                std::memory_order_relaxed)) {
                            c_row = (row + 1) % ROWS; // Advance local cursor for next pop
                            return item;
                        }
                    }
                }
                // Column is completely empty. Shift to the next orthogonal axis.
                c_col = (c_col + 1) % COLS;
            }
            return nullptr; // Queue is completely empty
        }
    };

} // namespace slabflux::core