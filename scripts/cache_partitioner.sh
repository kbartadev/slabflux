#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
#
# ============================================================================
# SLABFLUX SOFTWARE ENGINE
# Copyright (c) 2026 Kristóf Barta (https://github.com/kbartadev)
# ============================================================================
# PROPRIETARY AND SOURCE-AVAILABLE CODEBASE. ALL RIGHTS RESERVED.
#
# This source file and all constitutive programmatic expressions contained herein
# are the exclusive intellectual property of Kristóf Barta, established and
# distributed strictly under the conditions of the SLABFLUX SOURCE-AVAILABLE
# AND ECOSYSTEM LICENSE (the "License").
#
# TITLE TO AND OWNERSHIP OF THE SOFTWARE, THE ENGINE, CORE LOGIC, ARCHITECTURAL
# LAYOUTS, AND ALL ASSOCIATED INSIGHTS REMAIN SOLELY VESTED IN THE AUTHOR.
#
# ----------------------------------------------------------------------------
# TECHNICAL WARNING & SYSTEM ARCHITECTURE NOTICE
# ----------------------------------------------------------------------------
# This software utilizes architecture-specific hardware intrinsics and bypasses
# standard operating system protections. Incorrect integration or configuration
# may result in critical system instability, kernel panics, or irreversible
# physical hardware destruction.
#
# ----------------------------------------------------------------------------
# ABSOLUTE USAGE RESTRICTIONS & OPERATIONAL PROHIBITIONS
# ----------------------------------------------------------------------------
# ANY COMMERCIAL USE, INSTITUTIONAL INCLUSION (#include), MICRO-ARCHITECTURAL
# REPLICATION, STRUCTURAL SEQUENCE EXTRACTION, OR CORPORATE DEPLOYMENT IS
# STRICTLY PROHIBITED AND CONSTITUTES AN IMMEDIATE, WILLFUL INFRINGEMENT
# OF COPYRIGHT AND CONTRACTUAL BREACH.
#
# Execution by individual, independent developers is permitted strictly subject
# to the conditional grants, mandatory attributions, and structural limitations
# defined within the License.
#
# ----------------------------------------------------------------------------
# ABSOLUTE THIRD-PARTY INDEMNIFICATION
# ----------------------------------------------------------------------------
# TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE USER AGREES TO FULLY INDEMNIFY,
# DEFEND, AND HOLD HARMLESS THE AUTHOR FROM AND AGAINST ANY AND ALL THIRD-PARTY
# CLAIMS, LIABILITIES, DAMAGES, AND EXPENSES (INCLUDING LEGAL FEES) ARISING
# OUT OF OR CONCERNING INTELLECTUAL PROPERTY INFRINGEMENT, UNAUTHORIZED
# INTEGRATION, OR OPERATIONAL DEPLOYMENT OF THE SOFTWARE.
#
# THE USER ASSUMES SINGULAR AND TOTAL LIABILITY FOR THE DEFENSE OF ANY THIRD-PARTY
# INTELLECTUAL PROPERTY CLAIMS, STATUTORY VIOLATIONS, OR OPERATIONAL DAMAGES
# DERIVED FROM THE SOFTWARE'S LOCAL OR EXTERNAL EXECUTION.
#
# ----------------------------------------------------------------------------
# EXPRESS HARDWARE RISK ALLOCATION & DISCLAIMER (UCC CONSPICUOUS NOTICE)
# ----------------------------------------------------------------------------
# THE USER EXPRESSLY ACKNOWLEDGES AND AGREES THAT EXECUTION OF THIS SOFTWARE
# CARRIES AN INHERENT RISK OF TOTAL PHYSICAL HARDWARE FAILURE AND PERMANENT
# DESTRUCTION OF COMPUTING INFRASTRUCTURE. THE USER VOLUNTARILY ASSUMES ALL
# SUCH RISKS AS A CONDITION OF EXECUTION TO THE MAXIMUM EXTENT PERMITTED BY LAW.
#
# IN NO EVENT SHALL THE AUTHOR OR COPYRIGHT HOLDER BE LIABLE FOR ANY CLAIM,
# DAMAGES, OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT, OR
# OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION WITH THE SOFTWARE.
#
# THE USER EXECUTES THIS SOFTWARE AT THEIR OWN SOLE RISK. THE AUTHOR ENTIRELY
# DISCLAIMS ANY LIABILITY FOR SYSTEM INSTABILITY, KERNEL PANICS, LOSS OF DATA,
# TRADING LOSSES, OR 
# SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
#
# ============================================================================
# SLABFLUX SOFTWARE ENGINE
# Copyright (c) 2026 Kristóf Barta (https://github.com/kbartadev)
# ============================================================================
# SOURCE-AVAILABLE CODEBASE
#
# This source file is distributed under the conditions of the SLABFLUX 
# SOURCE-AVAILABLE AND ECOSYSTEM LICENSE (the "License").
#
# ----------------------------------------------------------------------------
# CRITICAL WARNING
# ----------------------------------------------------------------------------
# This module may execute outside standard OS mediation layers. Incorrect 
# integration, misconfiguration, or unsafe deployment can result in:
#
#   • irreversible data corruption
#   • kernel instability or panics
#   • NIC or PCIe bus desynchronization
#   • undefined hardware state transitions
#   • permanent loss of system integrity
#
# Use only in controlled environments with full understanding of the 
# architectural constraints and hardware implications.
#
# ----------------------------------------------------------------------------
# USAGE GUIDELINES
# ----------------------------------------------------------------------------
# Execution, integration, and deployment by developers is permitted strictly 
# subject to the conditional grants and structural limitations defined within 
# the License. Please refer to the License for full terms regarding corporate 
# deployment and replication.
#
# ----------------------------------------------------------------------------
# LIMITATION OF LIABILITY
# ----------------------------------------------------------------------------
# TO THE MAXIMUM EXTENT PERMITTED BY LAW, IN NO EVENT SHALL THE AUTHOR OR 
# COPYRIGHT HOLDER BE LIABLE FOR ANY CLAIM, DAMAGES, OR OTHER LIABILITY, 
# WHETHER IN AN ACTION OF CONTRACT, TORT, OR OTHERWISE, ARISING FROM, OUT OF, 
# OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
# ----------------------------------------------------------------------------
# DISCLAIMER OF WARRANTY
# ----------------------------------------------------------------------------
# THIS SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
#
# See accompanying LICENSE and NOTICE files for the integrated terms of use.
# ============================================================================.
#
# THIS SOFTWARE IS PROVIDED "AS IS" WITHOUT ANY REPRESENTATIONS OR WARRANTIES
# OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO ANY IMPLIED
# WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR
# NON-INFRINGEMENT OF THIRD-PARTY INTELLECTUAL PROPERTY RIGHTS.
#
# See accompanying LICENSE and NOTICE files for the integrated terms of use.
# ============================================================================
# Intel CAT (Cache Allocation Technology) Enforcer
set -euo pipefail

log_info() { echo -e "\e[32m[CAT]\e[0m $1"; }
log_err()  { echo -e "\e[31m[ERROR]\e[0m $1"; exit 1; }

[[ $EUID -ne 0 ]] && log_err "Root privileges required."

# 1. Verify pqos tool availability
if ! command -v pqos &> /dev/null; then
    log_err "pqos (intel-cmt-cat) not installed."
fi

# 2. Capability Check: Verify CAT support on physical CPU
if ! pqos -s | grep -q "L3 CAT details"; then
    log_err "CPU does not support L3 Cache Allocation Technology (CAT)."
fi

# 3. Create Class of Service (CLOS1)
# 0x3F0 reserves 6 specific ways in the L3 hierarchy for logic.
pqos -e "llc:1=0x3F0" || log_err "Failed to establish LLC mask."

# 4. Assign Cores 2-5 to CLOS1
pqos -a "llc:1=2,3,4,5" || log_err "Failed to assign cores to CLOS1."

log_info "L3 Cache successfully partitioned for Cores."
