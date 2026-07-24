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
 * @file admin_interface.hpp
 * @brief Administrative Control Interface.
 * @details Allows the non-deterministic Platform to issue deterministic 
 * commands to the Core (Snapshot, Pause, Reconfigure).
 */

#pragma once

#include <cstdint>

namespace slabflux::sys {

    enum class admin_cmd_type : uint32_t {
        TAKE_SNAPSHOT = 0x01,
        RELOAD_CONFIG = 0x02,
        FORCE_HALT    = 0x03,
        ENTER_PASSIVE = 0x04, // For High-Availability failover
        RESET_FAULTS  = 0x05,
        CLEAR_PARKING_LOTS = 0x06,
        UPDATE_PRECISION = 0x07,
        MANUAL_CHECKPOINT = 0x08,
        UPDATE_BASELINE = 0x09,
        UPDATE_CRITICAL_DRIFT = 0x0A,
        UPDATE_DRIFT_POLICY = 0x0B,
        TOGGLE_WEIGHTED_SANITIZATION = 0x0C,
        SET_DIVERGENCE_SNAPSHOT_THRESHOLD = 0x0D,
        HEALTH_CHECK = 0x0E,
        EXPORT_METRICS = 0x0F,
        TOGGLE_DRIFT_SMOOTHING = 0x10,
        REPLICATE_STATE = 0x11
    };

    struct admin_command {
        admin_cmd_type type;
        uint64_t payload;
    };
}
