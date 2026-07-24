# Foundation: Hotpatch Bridge (`slabflux/bridge/hotpatch_bridge.hpp`)

## 1. Architectural Justification
Restarting a trading node to deploy a new algorithm update incurs minutes of downtime and forfeits initialized TCP sessions. The `hotpatch_bridge` allows the execution of critical logic to be seamlessly rewritten at the silicon instruction level without bringing down the process.

## 2. Hardware Implementation Directives
- **Instruction-Level Redirection**: Exploits `mprotect(PROT_READ | PROT_WRITE | PROT_EXEC)` to make the `.text` segment temporarily writable.
- **Trampoline Injection**: The bridge identifies the entry point of the deprecated function and atomically overwrites the first 5 bytes with an unconditional relative jump (`JMP`) to the memory address of the newly loaded logic block (via `dlopen`).
- **Execution Fencing**: Executes a cross-core `IPI` (Inter-Processor Interrupt) or a tight `MFENCE` to flush the CPU's Instruction Pipeline, ensuring no thread is executing the half-overwritten instruction.

## 3. Bibliography & Proofs
1. **Makarov, A., & SUSE.** (2014). *kpatch: Dynamic Kernel Patching*. (Trampoline injection mechanics).
2. **Intel Corporation**. *Intel 64 and IA-32 Architectures Software Developer’s Manual, Volume 3A*. (Self-Modifying Code, cross-modifying code, and instruction cache invalidation).
3. **Corbet, J.** (2014). *Live patching the kernel*. LWN.net.