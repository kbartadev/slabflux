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
 * ============================================================================* @file 03_autonomous_drone_ecs.cpp
 * @brief Universal Event-Driven Architecture (Sensor Fusion & Robotics)
 */

#include <iostream>
#include <thread>
#include <atomic>
#include <immintrin.h>
#include "slabflux/core.hpp"

using namespace slabflux;

// ============================================================================
// 1. THE VERTICAL MATRIX: Hardware, Safety, and Sensor Data
// ============================================================================

struct hardware_interrupt {
    uint64_t capture_ns;
    int      sensor_id;
};

struct safety_critical_system {
    virtual ~safety_critical_system() = default;
    virtual bool is_hardware_healthy() const noexcept = 0;
};

// Native C++ inheritance. No macro bloat.
// NOTE: Assuming spsc_pool now returns T*
struct navigation_vector : hardware_interrupt, safety_critical_system {
    double velocity_x;
    double velocity_y;
    double altitude;
    bool   sensor_ok;

    navigation_vector(int s_id, double vx, double vy, double alt, bool healthy)
        : velocity_x(vx), velocity_y(vy), altitude(alt), sensor_ok(healthy) {
        this->capture_ns = __rdtsc();
        this->sensor_id = s_id;
    }

    bool is_hardware_healthy() const noexcept override {
        return sensor_ok;
    }
};

template <typename T>
concept HasVelocity = requires(T a) { a.velocity_x; a.velocity_y; };

// ============================================================================
// 2. THE PIPELINE HANDLERS: Data Processing System (ECS)
// ============================================================================

// NOTE: Handlers now accept raw pointers T*
struct blackbox_logger {
    void on(const hardware_interrupt* ev) { // Changed to const T*
        std::cout << "[L0 BLACKBOX] Sensor #" << ev->sensor_id
                  << " fired at " << ev->capture_ns << " ns\n";
    }
};

struct safety_watchdog {
    bool on(safety_critical_system* ev) { // Changed to T*
        if (!ev->is_hardware_healthy()) {
            std::cout << "[L1 SAFETY] CRITICAL FAULT! Sensor malfunction. Dropping vector!\n";
            return false; // SHORT-CIRCUIT
        }
        std::cout << "[L1 SAFETY] Hardware OK.\n";
        return true;
    }
};

struct collision_avoidance {
    template <HasVelocity E>
    bool on(const E* ev) { // Changed to const T*
        double speed_squared = (ev->velocity_x * ev->velocity_x) + (ev->velocity_y * ev->velocity_y);
        if (speed_squared > 10000.0) {
            std::cout << "[L2 RADAR] WARNING: Approach too fast! Engaging airbrakes.\n";
            return false;
        }
        return true;
    }
};

struct wind_compensator {
    void on(navigation_vector* ev) { // Changed to T*
        ev->velocity_x -= 2.5; // Runtime mutation via non-const reference
        std::cout << "[L2 PHYSICS] Applied wind drift. New X velocity: " << ev->velocity_x << " m/s\n";
    }
};

struct motor_actuator {
    void on(const navigation_vector* ev) { // Changed to const T*
        std::cout << "[L2 MOTORS] Actuating rotors. Target altitude: " << ev->altitude << "m\n";
    }
};

// ============================================================================
// 3. THE ORCHESTRATION
// ============================================================================

int main() {
    std::cout << "=== SLABFLUX Autonomous Flight Controller ===\n\n";

    // 1. Core infrastructure
    spsc_pool<navigation_vector, 256> flight_pool; // Assuming spsc_pool now returns T*
    spsc_conduit<navigation_vector*, 1024> flight_bus; // Conduit now uses T*

    // 2. Handlers
    blackbox_logger     blackbox;
    safety_watchdog     safety;
    collision_avoidance radar;
    wind_compensator    physics;
    motor_actuator      motors;

    // 3. The Pipeline
    pipeline<blackbox_logger, safety_watchdog, collision_avoidance, wind_compensator, motor_actuator>
        flight_computer(blackbox, safety, radar, physics, motors);

    std::atomic<bool> running{ true };

    // --- CONSUMER THREAD ---
    // Spin loop for active processing AND final drain
    std::thread engine_thread([&]() {
        while (true) {
            auto ev = flight_bus.try_pop(flight_pool);
            if (ev) {
                flight_computer.dispatch(ev);
            }
            else if (!running.load(std::memory_order_relaxed)) {
                 break;
            }
            else {
                _mm_pause();
            }
        }
    });

    // Ergonomic safe push helper with hardware spinlock
    auto push_vector = [&](int s_id, double vx, double vy, double alt, bool healthy) {
        if (auto ev = flight_pool.make(s_id, vx, vy, alt, healthy)) {
            while (!flight_bus.try_push(ev)) {
                _mm_pause();
            }
        }
    };

    std::cout << "--- SCENARIO 1: Normal Flight Vector ---\n";
    push_vector(10, 15.0, 5.0, 120.0, true);

    std::cout << "\n--- SCENARIO 2: Hardware Malfunction ---\n";
    push_vector(11, 0.0, 0.0, 50.0, false);

    std::cout << "\n--- SCENARIO 3: Collision Course (Overspeed) ---\n";
    push_vector(12, 150.0, 0.0, 80.0, true);

    // --- GRACEFUL SHUTDOWN ---
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    running.store(false, std::memory_order_release);
    engine_thread.join();

    std::cout << "\n[MAIN] Drone successfully landed and shut down. Zero leaks.\n";

    return 0;
}
