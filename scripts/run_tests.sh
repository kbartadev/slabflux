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
# Bare-Metal Ultra-Low Latency Test Suite Runner
set -euo pipefail

# --- COLOR PALETTE ---
COLOR_RESET="\033[0m"
COLOR_INFO="\033[1;34m"
COLOR_SUCCESS="\033[1;32m"
COLOR_WARN="\033[1;33m"
COLOR_ERROR="\033[1;31m"
COLOR_HEADER="\033[1;36m"

echo -e "${COLOR_HEADER}====================================================================${COLOR_RESET}"
echo -e "${COLOR_HEADER}          SLABFLUX -- TEST SUITE RUNNER                         ${COLOR_RESET}"
echo -e "${COLOR_HEADER}====================================================================${COLOR_RESET}"

# --- 1. PRIVILEGE & PATH ENFORCEMENT ---
IS_ROOT=false
if [[ $EUID -eq 0 ]]; then
   IS_ROOT=true
else
   echo -e "${COLOR_WARN}[WARN] Running without root privileges. Hardware tuning will be skipped.${COLOR_RESET}"
   echo -e "${COLOR_INFO}For best results, execute using: sudo ./run_tests.sh [gtest_arguments]${COLOR_RESET}"
fi

# Locate binary context dynamically
BUILD_DIR_NAME="build"
BINARY_NAME="all_slabflux"

# Detect the absolute path of the directory where this script resides
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &> /dev/null && pwd)"

# Define absolute target lookup tracks based strictly on the script's home location
LOCAL_BIN_PATH="${SCRIPT_DIR}/${BINARY_NAME}"
RELATIVE_BIN_PATH="${SCRIPT_DIR}/../${BUILD_DIR_NAME}/tests/${BINARY_NAME}"

if [[ -f "${LOCAL_BIN_PATH}" ]]; then
    cd "${SCRIPT_DIR}"
    EXEC_PATH="./${BINARY_NAME}"
elif [[ -f "${RELATIVE_BIN_PATH}" ]]; then
    # Dynamically pivot directly into the build track directory
    cd "$(dirname "${RELATIVE_BIN_PATH}")"
    EXEC_PATH="./${BINARY_NAME}"
else
    echo -e "${COLOR_ERROR}[FATAL] Executable '${BINARY_NAME}' not found.${COLOR_RESET}"
    echo -e "Evaluated lookups based on script location (${SCRIPT_DIR}):"
    echo -e "  -> Local Path   : ${LOCAL_BIN_PATH}"
    echo -e "  -> Relative Path: ${RELATIVE_BIN_PATH}"
    echo -e "Please verify your compiled binary output destination matches your constants."
    exit 1
fi

# --- 2. RECORD ORIGINAL SYSTEM STATES FOR RESTORATION ---
if [ "$IS_ROOT" = true ]; then
    echo -e "${COLOR_INFO}[SYSTEM] Backing up current workstation topology parameters...${COLOR_RESET}"
    ORIG_HUGEPAGES=$(sysctl -n vm.nr_hugepages)
    ORIG_REUSE=$(sysctl -n net.ipv4.tcp_tw_reuse)
    ORIG_GOVERNOR=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo "powersave")

    # Cleanup trap to guarantee restoration even if the tests crash or get cancelled
    cleanup_topology() {
        echo -e "\n${COLOR_INFO}[CLEANUP] Tearing down testbed environment and restoring defaults...${COLOR_RESET}"
        sysctl -w vm.nr_hugepages="${ORIG_HUGEPAGES}" >/dev/null
        sysctl -w net.ipv4.tcp_tw_reuse="${ORIG_REUSE}" >/dev/null

        if command -v cpupower &> /dev/null; then
            cpupower frequency-set -g "${ORIG_GOVERNOR}" >/dev/null 2>&1
        else
            for gov in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
                [ -f "$gov" ] && echo "${ORIG_GOVERNOR}" > "$gov" 2>/dev/null || true
            done
        fi
        echo -e "${COLOR_SUCCESS}[CLEANUP] Workstation profiles successfully restored.${COLOR_RESET}"
    }
    trap cleanup_topology EXIT

    # --- 3. HARDWARE-LEVEL ENVIRONMENT TUNING ---
    echo -e "${COLOR_INFO}[TUNING] Optimizing Linux kernel invariants for low-latency emulation...${COLOR_RESET}"

    # A. Maximize locked memory boundaries to eradicate std::bad_alloc
    ulimit -l unlimited
    echo -e "  -> Memory Locking Boundaries: ${COLOR_SUCCESS}UNLIMITED${COLOR_RESET}"

    # B. Refill 2MB HugePage hardware block rings
    sysctl -w vm.nr_hugepages=512 >/dev/null
    echo -e "  -> Kernel HugePages Allocated : ${COLOR_SUCCESS}512 (1GB RAM committed)${COLOR_RESET}"

    # C. Enable immediate TCP Port Reuse for loopback routing nodes
    sysctl -w net.ipv4.tcp_tw_reuse=1 >/dev/null
    echo -e "  -> Network TCP TIME_WAIT Reuse: ${COLOR_SUCCESS}ENABLED${COLOR_RESET}"

    # D. Lock CPU cores out of energy-saving throttling drops
    if command -v cpupower &> /dev/null; then
        cpupower frequency-set -g performance >/dev/null 2>&1
    else
        for gov in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
            [ -f "$gov" ] && echo "performance" > "$gov" 2>/dev/null || true
        done
    fi
    echo -e "  -> CPU Core Power Governors  : ${COLOR_SUCCESS}MAX PERFORMANCE${COLOR_RESET}"
fi

# --- 4. HIGH-PRIORITY TEST EXECUTION ---
echo -e "${COLOR_INFO}[LAUNCH] Booting SLABFLUX Test harness with Real-Time Scheduling...${COLOR_RESET}"
echo -e "${COLOR_HEADER}--------------------------------------------------------------------${COLOR_RESET}"

# Using nice -n -20 gives the runtime maximum possible desktop execution scheduling
# priority without triggering the dangerous SCHED_FIFO real-time starvation lockups.
if [ "$IS_ROOT" = true ]; then
    EXEC_CMD="nice -n -20 ${EXEC_PATH}"
else
    EXEC_CMD="${EXEC_PATH}"
fi

if $EXEC_CMD "$@"; then

    echo -e "${COLOR_HEADER}--------------------------------------------------------------------${COLOR_RESET}"
    echo -e "${COLOR_SUCCESS}[SUCCESS] All executed SLABFLUX targets satisfied invariants flawlessly.${COLOR_RESET}"
else
    echo -e "${COLOR_HEADER}--------------------------------------------------------------------${COLOR_RESET}"
    echo -e "${COLOR_ERROR}[FAILURE] Test suite runner identified tracking regressions.${COLOR_RESET}"
    exit 2
fi
