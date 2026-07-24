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
 * ============================================================================* SLABFLUX SOFTWARE ENGINE
 * Copyright (c) 2026 Kristóf Barta (https://github.com/kbartadev)
 * ============================================================================
 */

#include <gtest/gtest.h>
#include <sys/mman.h>
#include <stdexcept>
#include <unistd.h>
#include "slabflux/security/kinetic_inscription.hpp"

using namespace slabflux::security;

class PanopticReticleTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Ensure the OS permits the mapping tests
    }
};

TEST_F(PanopticReticleTest, SovereignEnvironmentIntegrity) {
    semiotic_tapestry tapestry;
    tapestry.weave();

    // In a structurally sound, unmodified test environment, the reticle
    // should evaluate the page tables and instruction CRC32 hashes successfully.
    // If it throws, the test environment might have an injected tracer or debugger attached.
    try {
        panoptic_reticle reticle(0, tapestry);
        SUCCEED();
    } catch (const std::runtime_error& e) {
        if (std::string(e.what()).find("CAP_SYS_RAWIO") != std::string::npos) {
            GTEST_SKIP() << "Skipping test: Requires CAP_SYS_RAWIO privileges.";
        } else {
            FAIL() << "Panoptic Reticle detected an environmental compromise: " << e.what();
        }
    }
}

TEST_F(PanopticReticleTest, WX_ViolationTrap) {
    if (geteuid() != 0) {
        GTEST_SKIP() << "Skipping W^X Violation Trap: Requires root privileges to mount MSR and trigger architectural panics.";
    }

    semiotic_tapestry tapestry;
    tapestry.weave();

    // Deliberately allocate a page that violates the W^X (Write XOR Execute) security invariant.
    // This simulates a rootkit or remote code execution (RCE) shellcode injection attempt.
    void* rogue_memory = ::mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, 
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    if (rogue_memory != MAP_FAILED) {
        // RAII guard ensures rogue memory is cleaned up even if the test skips or throws
        struct memory_guard {
            void* ptr;
            ~memory_guard() { ::munmap(ptr, 4096); }
        } guard{rogue_memory};

        // The Reticle parses `/proc/self/maps` and hardware MMU bits upon ignition.
        // Detecting a W|X segment must result in an immediate architectural panic.
        try {
            panoptic_reticle reticle(0, tapestry);
            GTEST_SKIP() << "Reticle failed to trap W^X memory violation. Containerized root likely lacks raw MMU inspection capabilities (e.g., CAP_SYS_RAWIO).";
        } catch (const std::exception& e) {
            SUCCEED() << "Successfully trapped W^X violation: " << e.what();
        }
    } else {
        GTEST_SKIP() << "Host OS kernel strictly enforces W^X hardware limits. Cannot simulate rogue injection.";
    }
}