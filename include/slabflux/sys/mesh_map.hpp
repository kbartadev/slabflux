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
 * @file mesh_map.hpp
 * @brief Internal Mesh Latency Calibration.
 */

// slabflux/core/mesh_map.hpp
#pragma once
#include <numa.h> // Linux libnuma
#include <memory>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::core {

    class mesh_map {
        int max_nodes_{0};
        std::unique_ptr<int[]> distance_matrix_; // Flattened matrix for O(1) cache-friendly access

    public:
        mesh_map() {
            // Hardware topology calibration under Linux
            if (numa_available() >= 0) {
                max_nodes_ = numa_max_node() + 1;
                distance_matrix_ = std::make_unique<int[]>(max_nodes_ * max_nodes_);

                for (int i = 0; i < max_nodes_; ++i) {
                    for (int j = 0; j < max_nodes_; ++j) {
                        distance_matrix_[i * max_nodes_ + j] = numa_distance(i, j);
                    }
                }
            }
        }

        // SLIT-based distance measurement
        int get_nearest_neighbor(int current_node) const noexcept {
            if (!distance_matrix_ || current_node >= max_nodes_) return current_node;

            int best_node = current_node;
            int min_distance = 999999;

            for (int target = 0; target < max_nodes_; ++target) {
                int dist = distance_matrix_[current_node * max_nodes_ + target];
                
                // Branchless Minimum Reduction.
                // Replaces textbook "if (dist < min)" which causes severe CPU pipeline 
                // branch mispredictions. We use sign-bit extraction to resolve the shortest path.
                int is_not_self = (target != current_node);
                int is_closer = (dist - min_distance) >> 31; // -1 (0xFFFFFFFF) if true, 0 otherwise
                int update_mask = is_not_self & is_closer;
                
                min_distance = (dist & update_mask) | (min_distance & ~update_mask);
                best_node = (target & update_mask) | (best_node & ~update_mask);
            }
            return best_node;
        }
    };

}
