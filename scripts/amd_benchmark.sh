#!/usr/bin/env bash
/#
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
# ============================================================================
# SlabFlux Deterministic Benchmark Mode
#
# These scripts configure the CPU into a deterministic, low‑noise state required
# for accurate SlabFlux‑RTE latency benchmarking.
#
# ⚠️ WARNING
# This mode is intended ONLY for controlled benchmarking environments.
# It disables SMT, C‑states, and CPU boost, and locks frequency.
#
# amd_restore.sh restores normal system behavior.

#!/usr/bin/env bash

echo "=== AMD Zen5 Deterministic Mode ==="

# 1) Switch amd_pstate to passive mode (REAL boost control)
if [ -f /sys/devices/system/cpu/cpufreq/amd_pstate/status ]; then
    echo "Setting amd_pstate to passive..."
    echo passive | sudo tee /sys/devices/system/cpu/cpufreq/amd_pstate/status > /dev/null
else
    echo "amd_pstate status file not found, skipping..."
fi

# 2) Set governor to performance
echo "Setting governor to performance..."
sudo cpupower frequency-set -g performance > /dev/null

# 3) Lock frequency to max non-boost clock
MAX=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq)
echo "Locking frequency to: $((MAX/1000)) MHz"
sudo cpupower frequency-set -d ${MAX} -u ${MAX} > /dev/null

# 4) Disable SMT
echo "Disabling SMT..."
echo off | sudo tee /sys/devices/system/cpu/smt/control > /dev/null

echo "=== DONE ==="
echo "System is now in deterministic mode."


