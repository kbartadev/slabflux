# Blueprint: pinned_allocator_spsc.hpp

## Architectural Overview
A highly specialized NUMA-aware ring allocator utilizing the shadow pointer watermark pattern to dramatically reduce MESI protocol cache invalidations.

## Core Logic & Mechanisms
- **Cached Watermarks**: Thread A stores a localized copy of Thread B's execution state. It only queries the heavy cross-core atomic variable when the localized bounds are completely exhausted.
- **Batch Releasing**: Optimizes network stream workloads (e.g. uring completions) by resetting multiple sequential indices simultaneously, collapsing multiple atomic executions into a single instruction.