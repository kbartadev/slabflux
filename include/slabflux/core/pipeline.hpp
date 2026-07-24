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
 * ============================================================================* SlabFlux Pipeline — Unified Integration Header
 *
 * This file glues together the entire SlabFlux-dispatcher system:
 *
 *   - typelist_algebra.hpp
 *   - ancestor_expansion.hpp
 *   - inverse_priority.hpp
 *   - phase_engine.hpp
 *   - context_vault.hpp
 *   - signature_router.hpp
 *   - dispatch_unroller.hpp
 *
 * pipeline.hpp does NOT contain logic — only the integration of modules
 * and the user pipeline class that calls the dispatch_unroller module.
 *
 * To be placed in the core/ directory:
 *   slabflux/core/pipeline.hpp
 */

#pragma once
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/pipeline/typelist_algebra.hpp"
#include "slabflux/pipeline/ancestor_expansion.hpp"
#include "slabflux/pipeline/inverse_priority.hpp"
#include "slabflux/pipeline/phase_engine.hpp"
#include "slabflux/pipeline/context_vault.hpp"
#include "slabflux/pipeline/signature_router.hpp"
#include "slabflux/pipeline/dispatch_unroller.hpp"

namespace slabflux::core {

    /*
     * The pipeline class is built on the dispatch_unroller module.
     * dispatch_unroller.hpp already contains:
     *
     *   template <typename... Handlers>
     *   class pipeline { ... };
     *
     * Therefore, no further implementation here — just forward.
     *
     * If you want to keep the pipeline class separate, then
     * the pipeline definition in dispatch_unroller.hpp
     * can be moved here, leaving only the internal unroller functions there.
     *
     * Current version: the pipeline definition is in dispatch_unroller.hpp.
     */

} // namespace slabflux
