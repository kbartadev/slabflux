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

#ifdef __linux__
#include <liburing.h>
#include "slabflux/io/uring_shim.hpp" // For uring_shim functions
#endif
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::core {

    class core {
#ifdef __linux__
        struct io_uring ring_;
#endif
        bool io_enabled_ = false;

    public:
        // io_uring initialization
        void ignite_async_io() {
#ifdef __linux__
            // Queue size 1024, using IORING_SETUP_SQPOLL for syscall-free operation.
            // The kernel will poll the submission queue on a dedicated SQ kthread!
            // uring_shim::ring_init throws on failure, so no need for if (ret < 0)
            slabflux::io::uring_shim::ring_init(1024, &ring_, IORING_SETUP_SQPOLL, 0); // sq_idle = 0 for default
            // if (ret < 0) {
            //     handle_critical_error("Failed to initialize io_uring with SQPOLL");
            // }
            io_enabled_ = true;
#else
            // Windows fallback simulation for compatibility,
            // but Linux behavior is guaranteed to be syscall-free.
            io_enabled_ = false;
#endif
        }

        ~core() {
#ifdef __linux__
            if (io_enabled_) {
                slabflux::io::uring_shim::ring_exit(&ring_);
            }
#endif
        }
    };

}
