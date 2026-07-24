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

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <gtest/gtest.h>

#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/memory.hpp"
#include "slabflux/transport/wire_protocol.hpp"
#include "slabflux/io/shm_duplex.hpp"

/**
 * @brief SHM Duplex Physics Audit.
 */
TEST(ShmDuplexTest, PhysicalResidencyAudit) {
    try {
        // Primary side: initializes memory segments
        slabflux::io::shm_duplex<512> primary("sf_shm_duplex_audit", true);
        
        // Requirement 1: Structure must be 64-byte aligned for MESI efficiency
        EXPECT_EQ(reinterpret_cast<uintptr_t>(&primary) % 64, 0);
        
        // Requirement 2: Size must be a multiple of cache-line to prevent aliasing
        EXPECT_EQ(sizeof(primary) % 64, 0);
        
        // Joiner side: attaches to existing segments
        slabflux::io::shm_duplex<512> joiner("sf_shm_duplex_audit", false);
        EXPECT_EQ(reinterpret_cast<uintptr_t>(&joiner) % 64, 0);
        EXPECT_EQ(sizeof(joiner) % 64, 0);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "SHM skipped (permissions or limits): " << e.what();
    }
}
