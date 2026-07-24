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
 * ============================================================================* @file tls_record_extractor.hpp
 * @brief Defragments TLS 1.3 Encrypted Records from raw TCP Streams.
 */

#pragma once
#include <cstdint>
#include <string_view>
#include "slabflux/transport/baremetal_parser.hpp"
#include "slabflux/core/endian.hpp"

namespace slabflux::security {

    struct tls_record_event {
        uint32_t connection_id{0};
        uint8_t  content_type{0};
        uint16_t version{0};
        std::string_view ciphertext;
        
        size_t total_bytes_consumed{0};

        void reset() noexcept {
            total_bytes_consumed = 0;
            content_type = 0;
            ciphertext = {};
        }
    };

    struct tls_record_extractor {
        static SLAB_HOT transport::parser_status parse(std::string_view window, tls_record_event& out_event) noexcept {
            if (window.size() < 5) return transport::parser_status::INCOMPLETE;

            // Read TLS 5-byte header
            uint16_t length = core::endian::network_to_host16(*reinterpret_cast<const uint16_t*>(window.data() + 3));
            
            if (window.size() < 5u + length) return transport::parser_status::INCOMPLETE;
            if (length > 16384 + 256) return transport::parser_status::ERROR; // RFC 8446 Max Record Limit

            out_event.content_type = window[0];
            out_event.version = core::endian::network_to_host16(*reinterpret_cast<const uint16_t*>(window.data() + 1));
            out_event.ciphertext = std::string_view(window.data() + 5, length);
            out_event.total_bytes_consumed = 5 + length;
            
            return transport::parser_status::OK;
        }
    };

} // namespace slabflux::security