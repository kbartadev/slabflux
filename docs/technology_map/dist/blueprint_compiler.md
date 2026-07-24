# Blueprint: Metadata Compiler Architecture

## Architectural Overview
The `slabflux_meta` compiler validates the strict, O(1) determinism of the pipeline at build-time. It completely replaces runtime RTTI (Run-Time Type Information) and reflection with a proprietary static AST analyzer that generates pure SFINAE-based jump tables.

## Core Components
- **AST Parsing Engine (`meta_compiler.cpp`)**: Sweeps the target source tree for strictly structured C++ structures and C++20 concepts to statically identify all event types and handler dependencies without invoking Clang/GCC plugin overhead.
- **Adversarial Validation (`meta_compiler_test.cpp`)**: A rigorous build-sequence validator that forcefully injects infinite loops, cyclic DAG dependencies, and memory-poisoned struct definitions to prove the compiler's resilience before allowing the primary engine build to proceed.
- **Duck-Typing Jump Tables**: Generates perfectly flattened, switchless routing definitions embedded directly into `slabflux_generated/` headers. It maps generic network payloads into strict memory representations utilizing compile-time evaluated templates.