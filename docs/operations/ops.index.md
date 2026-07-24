# SLABFLUX: Operations & SRE Guide

This guide defines the operational procedures required to maintain the deterministic health and sub-microsecond performance of the SlabFlux Runtime Environment. 

## 1. Physical Environment Preparation
Before engine ignition, the underlying silicon must be hardened to prevent OS-induced jitter.

| Task | Reference |
| :--- | :--- |
| **OS Hardening & Tuning** | [Operating System Hardening & STS](../spec.29.operating.system.hardening.md) |
| **Physical Core Isolation** | [shield_cores.sh](../../scripts/shield_cores.sh) / [jitter_shield.sh](jitter_shield.sh) |
| **Cache Partitioning** | [cache_partitioner.sh](../../scripts/cache_partitioner.sh) (Intel CAT) |
| **Interrupt Affinity** | [interrupt_lock.sh](../../scripts/interrupt_lock.sh) |

## 2. Deployment & Ignition
Procedures for cold-booting the cluster and performing rolling updates without state loss.

| Task | Resource |
| :--- | :--- |
| **Deployment Runbook** | [Deployment And Kernel Tuning](ops.deployment.md) |
| **Binary Config Generation** | [sf_cfg_gen.py](../../scripts/sf_cfg_gen.py) Utility |
| **Master Initialization** | [sovereign_init.sh](../../scripts/sovereign_init.sh) Bootstrapper |

## 3. Runtime Observability
Monitoring deterministic invariants and micro-latency telemetry in production.

| Domain | Focus |
| :--- | :--- |
| **Health Audits** | Real-time state hash verification via `audit_ledger`. |
| **Performance Metrics** | Micro-latency tracking via `blackbox_recorder`. |
| **Numerical Stability** | Drift monitoring and lane-by-lane divergence analysis. |

## 4. Incident Response & Recovery
Procedures for handling system panics and performing post-mortem analysis.

| Resource | Description |
| :--- | :--- |
| **Troubleshooting Guide** | [SRE Runbook](ops.sre.runbook.md) |
| **Post-Mortem Replay** | Reconstructing failure states via `replay_saga`. |
| **Core Dumps** | Analyzing hardware telemetry snapshots from a `PANIC` event. |

---
