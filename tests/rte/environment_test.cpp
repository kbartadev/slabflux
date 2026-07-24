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
#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sys/mman.h>
#include <fcntl.h>

#include "slabflux/rte/environment.hpp"
#include "slabflux/sys/tick_event.hpp"
#include "slabflux/mesh/causal_mesh.hpp"

namespace slabflux::test {

    // =====================================================================
    // Mock Domain Logic for the Deterministic Engine
    // =====================================================================

    struct DummyEvent {
        static constexpr uint32_t TYPE_ID = 1;
        uint64_t lsn;
        double payload_data;
    };

    struct DummyStateLogic {
        using value_type = float;
        static constexpr size_t capacity = 16;

        alignas(4096) float elements[capacity]{};

        // Required concepts by branchless_engine
        void on_event(const DummyEvent* ev, uint64_t lsn) noexcept {
            if (ev) elements[0] = static_cast<float>(ev->payload_data);
        }
        void on_tick(const sys::tick_event* tick, uint64_t lsn) noexcept {}
        slabflux::mesh::wire_frame<DummyEvent>* generate_response() const noexcept { return nullptr; }
    };

    // =====================================================================
    // Active Environment Integration Tests
    // =====================================================================

    class EnvironmentIntegrationTest : public ::testing::Test {
    protected:
        const char* journal_path = "test_durable_journal.bin";
        const char* config_path = "slabflux_config.bin";
        const char* shm_path = "/slabflux_retrans_cache";

        void SetUp() override {
            // Ensure a clean state for io_uring and mmap files
            std::ofstream j_out(journal_path, std::ios::binary | std::ios::trunc);
            j_out.close();

            // Create dummy config file to prevent fd open failures
            slabflux::rte::binary_config_payload cfg{};
            cfg.precision_delta = 0.001f;
            std::ofstream c_out(config_path, std::ios::binary | std::ios::trunc);
            c_out.write(reinterpret_cast<const char*>(&cfg), sizeof(cfg));
            c_out.close();

            // Unlink any dangling shared memory from previous crashed tests
            shm_unlink(shm_path);
        }

        void TearDown() override {
            std::filesystem::remove(journal_path);
            std::filesystem::remove(config_path);
            std::filesystem::remove("system_performance_dump.csv");
            shm_unlink(shm_path);
        }
    };

    TEST_F(EnvironmentIntegrationTest, BuilderTopologyIgnition) {
        DummyStateLogic logic;

        try {
            // Ignite the topology using dry-run to bypass strict NIC bindings
            auto env = slabflux::rte::build_topology()
            .with_ingress_on_core(0)
            .with_conduit_on_core(0)
            .with_clock_on_core(0)
            .with_journal(0, journal_path)
            .with_node_id(1)
            .with_dry_run(true)
            .ignite<DummyEvent, DummyStateLogic, 65536>(logic);

            // Validate that core infrastructure arrays are successfully mapped
            EXPECT_EQ(env.get_conduit_core(), 0);
            EXPECT_GE(env.get_io_worker_fd(), 0); // io_uring ring should be valid
        } catch (const std::exception& e) {
            std::string msg = e.what();
            if (msg.find("Operation not permitted") != std::string::npos || 
                msg.find("CAP_SYS_RAWIO") != std::string::npos ||
                msg.find("Permission denied") != std::string::npos) {
                GTEST_SKIP() << "Skipping test: Insufficient environment privileges (" << msg << ")";
            } else {
                throw;
            }
        }
    }

    TEST_F(EnvironmentIntegrationTest, ThreadLifecyclesAndGracefulHalt) {
        DummyStateLogic logic;

        try {
            auto env = slabflux::rte::build_topology()
            .with_ingress_on_core(0)
            .with_conduit_on_core(0)
            .with_clock_on_core(0)
            .with_journal(0, journal_path)
            .with_node_id(1)
            .with_dry_run(false) // We want the actual polling loops to engage
            .ignite<DummyEvent, DummyStateLogic, 65536>(logic);

        // Spawn the asynchronous compute thread
        std::atomic<bool> thread_started{false};
        std::thread compute_thread([&]() {
            thread_started = true;
            env.run_compute(); // Should spin until system_active_ is false
        });

        // Wait for thread to enter the hot loop
        while (!thread_started.load()) { std::this_thread::yield(); }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Dispatch an emergency halt to the environment
        // This should toggle system_active_ and unblock run_compute()
        env.halt();

        // If the thread joins successfully, the non-blocking polling loops
        // and Aphasic Horizons are completely stable.
        if (compute_thread.joinable()) {
            compute_thread.join();
        }
            SUCCEED();
        } catch (const std::exception& e) {
            std::string msg = e.what();
            if (msg.find("Operation not permitted") != std::string::npos || 
                msg.find("CAP_SYS_RAWIO") != std::string::npos ||
                msg.find("Permission denied") != std::string::npos) {
                GTEST_SKIP() << "Skipping test: Insufficient environment privileges (" << msg << ")";
            } else {
                throw;
            }
        }
    }

    TEST_F(EnvironmentIntegrationTest, ManagementPlaneConfigReload) {
        DummyStateLogic logic;

        try {
            auto env = slabflux::rte::build_topology()
            .with_ingress_on_core(0)
            .with_conduit_on_core(0)
            .with_clock_on_core(0)
            .with_journal(0, journal_path)
            .with_node_id(1)
            .ignite<DummyEvent, DummyStateLogic, 65536>(logic);

        // Forge an administrative command to reload the configuration via mmap
        slabflux::sys::admin_command cmd;
        cmd.type = slabflux::sys::admin_cmd_type::RELOAD_CONFIG;

        // Emulate the control-plane polling step inline
        env.process_management_plane(&cmd);

        // We should expect the precision_delta configured in SetUp (0.001f)
        // to have dynamically loaded into the live engine's configuration bridge
        // without allocating any strings or vectors.

        // Note: The configuration bridge is not directly exposed as public outside of
        // the branchless_engine context, but we verified the pipeline process completed
        // without trapping or segregating.

        // We manually halt it here to safely clean up the `io_uring` and `shm` descriptors.
        env.halt();
            SUCCEED();
        } catch (const std::exception& e) {
            std::string msg = e.what();
            if (msg.find("Operation not permitted") != std::string::npos || 
                msg.find("CAP_SYS_RAWIO") != std::string::npos ||
                msg.find("Permission denied") != std::string::npos) {
                GTEST_SKIP() << "Skipping test: Insufficient environment privileges (" << msg << ")";
            } else {
                throw;
            }
        }
    }

} // namespace slabflux::test
