/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * @file asymmetric_dispersion_queue.hpp
 * @brief World-first Asymmetric Dispersion Matrix (ADM) MPSC Queue.
 */

#pragma once

#include <atomic>
#include <thread>
#include <cstddef>
#include <functional>
#include "slabflux/core/physical_layout.hpp"
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::core {

    template <typename T, std::size_t CapacityRows = 256>
    class asymmetric_dispersion_queue {
    private:
        // Matrix column count is fixed exactly to the size of one L1 Cache Line.
        static constexpr std::size_t COLS = CACHE_LINE_SIZE / sizeof(std::atomic<T*>);
        static constexpr std::size_t ROWS = CapacityRows;

        // Isolated structure: guarantees that rows do not physically share a cache line
        struct alignas(CACHE_LINE_SIZE) cache_row {
            std::atomic<T*> slots[COLS];
            
            constexpr cache_row() noexcept {
                for (std::size_t i = 0; i < COLS; ++i) {
                    slots[i].store(nullptr, std::memory_order_relaxed);
                }
            }
        };

        cache_row matrix_[ROWS];

        // Unique seed for spatial dispersion of producers across the matrix
        static std::size_t producer_seed() noexcept {
            std::size_t h = std::hash<std::thread::id>{}(std::this_thread::get_id());
            h ^= h >> 12; h ^= h << 25; h ^= h >> 27;
            return h;
        }

        // Consumer internal, isolated cursors (non-atomic as there is only 1 consumer)
        std::size_t c_row_{0};
        std::size_t c_col_{0};

    public:
        asymmetric_dispersion_queue() = default;
        ~asymmetric_dispersion_queue() = default;

        asymmetric_dispersion_queue(const asymmetric_dispersion_queue&) = delete;
        asymmetric_dispersion_queue& operator=(const asymmetric_dispersion_queue&) = delete;

        /**
         * @brief Wait-Free Bounded Push.
         * @details Searches for a VACUUM cell via dispersive traversal. No CAS loop.
         */
        bool push(T* payload) noexcept {
            static thread_local std::size_t p_row = producer_seed() % ROWS;
            static thread_local std::size_t p_col = 0;

            for (std::size_t attempt = 0; attempt < ROWS; ++attempt) {
                for (std::size_t c = 0; c < COLS; ++c) {
                    std::size_t col = (p_col + c);
                    if (col >= COLS) col -= COLS; // Branchless (modulo-free) rotation

                    T* expected_empty = nullptr;
                    
                    // No loop! If it fails, the cell is occupied, check the next one immediately.
                    if (matrix_[p_row].slots[col].compare_exchange_strong(
                            expected_empty, payload, 
                            std::memory_order_release, 
                            std::memory_order_relaxed)) {
                        
                        p_col = col + 1; // The next insertion continues from here
                        return true;
                    }
                }
                // The current row (cache line) is saturated or contested. 
                // Jump to the next separated line.
                if (++p_row >= ROWS) p_row = 0;
            }
            return false; // Queue capacity exhausted
        }

        /**
         * @brief Lock-Free Bounded Pop (Zero-CAS).
         * @details As a Single-Consumer model, the Load/Store pair is axiomatically safe,
         * saving RFO transactions on the memory bus.
         */
        T* pop() noexcept {
            for (std::size_t attempt = 0; attempt < ROWS; ++attempt) {
                for (std::size_t c = 0; c < COLS; ++c) {
                    std::size_t col = (c_col_ + c);
                    if (col >= COLS) col -= COLS;

                    // Acquire: Ensures that the payload has fully arrived in RAM
                    T* item = matrix_[c_row_].slots[col].load(std::memory_order_acquire);
                    
                    if (item != nullptr) {
                        // Zero-CAS release! 
                        // Producers do not overwrite the PLASMA state, so this is atomically isolated.
                        matrix_[c_row_].slots[col].store(nullptr, std::memory_order_release);
                        
                        // Positioning the cursor to the next element for continuous scanning
                        c_col_ = col + 1;
                        return item;
                    }
                }
                c_col_ = 0;
                if (++c_row_ >= ROWS) c_row_ = 0;
            }
            return nullptr; // The entire matrix is empty
        }
    };

} // namespace slabflux::core
