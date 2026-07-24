============================================================================
SLABFLUX NOTICE
============================================================================

SLABFLUX ENGINE
Copyright (C) 2026 Kristóf Barta

All rights reserved. This software engine, including its source code, compiled
binaries, micro-architectural design, and algorithmic expressions (collectively 
referred to as "the Engine"), is the intellectual property of Kristóf Barta, 
published and distributed strictly under the terms and conditions of the 
accompanying LICENSE.

============================================================================
LICENSE AND USAGE CONDITIONS
============================================================================

The Engine is published and distributed exclusively under the terms of the
SLABFLUX SOURCE-AVAILABLE AND ECOSYSTEM LICENSE.

By downloading, compiling, or executing any portion of the Engine, the User
acknowledges and accepts the terms of the LICENSE. Corporate, institutional, 
or commercial production deployment is strictly prohibited unless authorized 
via a separate, explicit written agreement with the Author.

============================================================================
HARDWARE & PERFORMANCE ADVISORY
============================================================================

This software utilizes architecture-specific hardware intrinsics and operates
through low-level execution paths that reduce or bypass standard operating 
system mediation layers (e.g., kernel-bypass I/O, SQPOLL) to achieve 
ultra-low latency.

The User expressly acknowledges that deploying high-performance, hardware-proximate 
software carries inherent operational risks, including but not limited to 
system instability, unusual thermal stress, or data loss. The User assumes 
all risks associated with the integration and execution of this Engine.

============================================================================
CONFIGURATION SCRIPTS & KERNEL TUNING
============================================================================

This repository may contain automated deployment scripts, bare-metal bootstrappers,
and low-latency kernel tuning commands (e.g., instructions involving CPU isolation,
mitigations=off, or direct peripheral register manipulation). 

These artifacts are provided as functional reference examples only. Executing 
any configuration, tuning parameter, or modification script is a voluntary act 
of system administration performed entirely at the User's own discretion and risk.

============================================================================
LIMITATION OF LIABILITY
============================================================================

The Engine is provided "AS IS", without warranty of any kind. For the complete
limitation of liability, warranty disclaimers, and proprietary terms, please 
refer to the accompanying LICENSE.md file, which is incorporated by reference.
============================================================================
