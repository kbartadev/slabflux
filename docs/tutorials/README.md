# SlabFlux Runtime Engine Tutorials

Welcome to the SlabFlux tutorial directory. This documentation system provides a rigorous, codebase-driven education on developing with the SlabFlux RTE, a deterministic, $O(1)$ latency-bound runtime engine.

## Directory Structure

*   **`master_curriculum.md`**
    The definitive syllabus. It maps the verified `slabflux/` subsystems to specific tutorial categories and outlines the foundational architecture of the runtime.
    
*   **`learning_path.md`**
    A sequential guide for developers, broken down into Beginner, Intermediate, Advanced, and Expert stages, complete with required readings and milestones.

*   **`tut.*.md`**
    The individual tutorial modules covering specific subsystems. 
    *   `1.x`: Core Memory & Routing Mechanics
    *   `2.x`: Runtime Environment & Orchestration
    *   `3.x`: Vectorized Compute & AI
    *   `4.x`: Hardware Isomorphism & Telemetry
    *   `5.x`: I/O, Transport, & Networking
    *   `6.x`: Supplemental Resilience
    *   `7.x`: Advanced Gateway & Domain Computing
    *   `8.x`: Timeline Management & String Mechanics

*   **`exercises_solutions.md`**
    Practical, compilable C++20 code solutions demonstrating how to compose multiple SlabFlux subsystems (like `mpmc_conduit`, `round_robin_switch`, and `spsc_pool`) into complete applications.