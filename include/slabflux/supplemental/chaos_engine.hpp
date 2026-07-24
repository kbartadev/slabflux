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
#include <random>
#include <cstdint>
#include "slabflux/core/wire_frame_lsn.hpp"

namespace slabflux::supplemental {

/**
 * @brief Fault injector for hardening the distributed board.
 * @details Injects non-deterministic failures into a deterministic system 
 * to verify recovery path integrity.
 */
class chaos_engine {
private:
    std::mt19937 gen_{std::random_device{}()};
    float drop_probability_ = 0.0f;
    float corruption_probability_ = 0.0f;

public:
    void set_probabilities(float drop, float corrupt) {
        drop_probability_ = drop;
        corruption_probability_ = corrupt;
    }

    /**
     * @brief Processes a frame and decides if it should be dropped or corrupted.
     * @return True if the frame survives, false if it is dropped.
     */
    template<typename T>
    bool process_outgoing(core::wire_frame_lsn<T>& frame) {
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);

        // 1. Packet Loss Simulation (Triggers NACK/Snapshot)
        if (dist(gen_) < drop_probability_) {
            return false; // Packet "lost" on the wire
        }

        // 2. Bit-flip Simulation (Triggers Secure Ingress/Checksum failures)
        if (dist(gen_) < corruption_probability_) {
            auto* bytes = reinterpret_cast<uint8_t*>(&frame.payload);
            bytes[gen_() % sizeof(T)] ^= 0xFF; 
        }

        return true;
    }
};

} // namespace slabflux::supplemental