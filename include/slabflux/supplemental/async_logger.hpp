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

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "../core.hpp"

namespace slabflux::supplemental {

/**
 * @brief Asynchronous log event structure.
 */
struct log_event {
    uint64_t timestamp_ns;
    uint16_t message_id;    // Which format string to use
    uint64_t arg1;          // Raw parameters (e.g., ID, Price)
    uint64_t arg2;
};

/**
 * @brief Asynchronous log processing node.
 */
template <typename TargetConduit>
class async_logger_node {
    TargetConduit& conduit_;
    bool is_running_{false};
    std::vector<std::string> format_strings_;

   public:
    explicit async_logger_node(TargetConduit& conduit) : conduit_(conduit) {}

    uint16_t register_message(const std::string& format_str) {
        format_strings_.push_back(format_str);
        return static_cast<uint16_t>(format_strings_.size() - 1);
    }

    void run() noexcept {
        is_running_ = true;
        uint64_t idle_polls = 0;

        while (is_running_) {
            typename TargetConduit::value_type ev;
            if (conduit_.try_pop(ev)) {
                idle_polls = 0;
                if (ev->message_id < format_strings_.size()) {
                    std::string msg = format_strings_[ev->message_id];

                    size_t pos = msg.find("{}");
                    if (pos != std::string::npos) msg.replace(pos, 2, std::to_string(ev->arg1));
                    pos = msg.find("{}");
                    if (pos != std::string::npos) msg.replace(pos, 2, std::to_string(ev->arg2));

                    std::cout << "[LOG " << ev->timestamp_ns << "] " << msg << "\n";
                }
            } else {
                if (++idle_polls < 1024) {
                    _mm_pause();
                } else {
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
            }
        }
    }

    void stop() { is_running_ = false; }
};

/**
 * @brief High-frequency logging client.
 */
template <typename Domain, typename TargetConduit>
class logger_client {
    Domain& domain_;
    TargetConduit& conduit_;

   public:
    logger_client(Domain& domain, TargetConduit& conduit) : domain_(domain), conduit_(conduit) {}

    SLAB_FORCE_INLINE void log(uint16_t message_id, uint64_t arg1 = 0, uint64_t arg2 = 0) noexcept {
        if (auto ev = domain_.template make<log_event>()) {
            ev->timestamp_ns = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            ev->message_id = message_id;
            ev->arg1 = arg1;
            ev->arg2 = arg2;
            conduit_.push(std::move(ev));
        }
    }
};

}  // namespace slabflux::supplemental
