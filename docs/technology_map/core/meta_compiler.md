# SlabFlux Compiler: Meta Compiler (`meta_compiler.cpp`)

## 1. Architectural Overview
C++ lacks native static reflection, traditionally forcing developers to use slow, dynamic RTTI (`dynamic_cast`) or bloated macros to register event handlers. The `slabflux_meta` compiler is an offline AST (Abstract Syntax Tree) parser that physically rewrites C++ code to generate perfect, O(1) execution topologies.

## 2. Compile-Time AST Parsing
The Meta Compiler integrates seamlessly into the CMake build sequence.
- Before GCC/Clang compiles the business logic, `slabflux_meta` parses the source headers to identify classes tagged with `SLAB_LOGIC_EXPERT`.
- It analyzes their inheritance structures, event footprint signatures (`on_event(Event*)`), and context requirements.

## 3. Typelist Synthesis
Once analyzed, the compiler generates a static `#pragma once` C++ header containing `slabflux::typelist<...>` matrices.
- It physically writes out the 7D Cartesian Dispatch Map (`proof.md`), sorting the event handlers into a topological DAG.
- This generated header is then injected into `pipeline.hpp` during the actual compilation phase.

## 4. O(1) Performance Guaranty
By moving type-resolution completely outside of the runtime environment, the Meta Compiler allows the `branchless_engine` to invoke hundreds of nested handlers with 0 overhead. The SFINAE logic utilizes the compiler-generated typelists to flatten the entire routing structure directly into sequential AVX assembly blocks.