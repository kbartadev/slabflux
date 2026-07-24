# SRE Runbook: SLABFLUX

## Incident: Pipeline Stall

**Check:** CPU is 100% but throughput is 0.
**Diagnosis:** Producer is spinning because a conduit is full.
**Action:** Identify the downstream consumer. Check for blocking calls such as I/O or locks in handlers.

## Incident: Memory Saturation

**Check:** `domain.make()` returns `nullptr`.
**Diagnosis:** Memory leak in user logic. Pointers are not going out of scope.
**Action:** Inspect stateful handlers. Verify events are not being stored in long-lived containers.

## Incident: SIGSEGV in `allocate_raw`

**Check:** Application crash during allocation.
**Diagnosis:** Buffer overflow in a neighboring slab cell has corrupted `next_index`.
**Action:** Run the binary with AddressSanitizer (ASAN) to find the illegal memory write.
