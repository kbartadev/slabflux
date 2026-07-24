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
 * @file industrial_jitter_test.cpp

 * SLABFLUX
 * Copyright (c) 2026 Kristóf Barta. All rights reserved.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 * Absolute Liability Limitation & Full Terms: See DISCLAIMER, NOTICE, LICENSE.
 */

#pragma once
#include <immintrin.h>
#include "slabflux/core/mpmc_sharded_conduit.hpp"
#include "slabflux/meta.hpp"

namespace slabflux::automation::detail {

    /**
     * @brief Zero-overhead bridge matching io:uring_egress's PipelineLogic interface.
     * @details Forwards frames directly from the io_uring buffer selection ring into the automation ring.
     */
    template <typename EventType, std::size_t Capacity, std::size_t Lanes>
    class conduit_proxy {
    private:
        core::mpmc_sharded_conduit<core::tagged_pointer, Capacity, Lanes>& target_conduit_;

    public:
        explicit conduit_proxy(core::mpmc_sharded_conduit<core::tagged_pointer, Capacity, Lanes>& conduit) noexcept
        : target_conduit_(conduit) {}

        /**
         * @brief Strict signature matching required by io:uring_egress.
         */
        SLAB_FORCE_INLINE bool on_raw_frame(EventType* frame, [[maybe_unused]] int bytes_received) noexcept {
            if (SL_EXPECT_FALSE(!frame)) return false;

            // Pack the unique type ID identifier into the atomic transport slice
            core::tagged_pointer packed = core::tagged_pointer::pack(EventType::ID, frame);

            // Try to submit to the sharded conduit matrix
            return target_conduit_.try_push(packed);
        }
    };

} // namespace slabflux::automation::detail
