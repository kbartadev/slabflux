# SlabFlux Transport: Stateful Baremetal JSON Parser (`baremetal_json_parser.hpp`)

## 1. Architectural Overview
Parsing unstructured JSON off the wire traditionally involves recursive descent algorithms or allocating massive AST (Abstract Syntax Tree) structures. Both approaches are fundamentally incompatible with deterministic, high-frequency execution limits.

The `baremetal_json_parser` provides a 100% **Stateful, Resumable Deterministic Finite Automaton (DFA)**. It does not allocate strings or DOM nodes. Instead, it extracts the JSON topology into a contiguous, pre-allocated "Token Tape" (`json_frame`).

## 2. The Contiguous Token Tape
Instead of pointers referencing child objects, the parser records structural events into an array of `json_token` structures:

```cpp
struct json_token {
    type t;             // OBJECT, ARRAY, STRING, NUMBER, KEY, etc.
    uint32_t start;     // Offset in the original buffer
    uint32_t length;    // Token string length
    uint32_t size;      // Direct child count
    int32_t parent;     // Index of the parent token
};
```
Because all tokens are mapped to physical offsets in the network buffer (`std::string_view`), zero bytes are copied during parsing. Extracting a string is an $O(1)$ projection of the original physical memory.

## 3. Computed Gotos (Threaded Code)
To parse JSON at extreme velocities without recursive descent, the parser relies on **Computed Gotos**:

```cpp
static const void* const dispatch[] = {
    &&L_ROOT_VALUE, &&L_OBJ_KEY, &&L_OBJ_COLON, &&L_IN_STRING, ...
};
goto *dispatch[(uint8_t)frame.state];
```
By routing execution through an array of instruction pointers, the parser entirely bypasses the CPU's branch prediction pipeline. This ensures that chaotic JSON structures (e.g., deeply nested heterogeneous arrays) do not cause pipeline stalls.

## 4. Hardware-Accelerated Resumability
Facing the public internet requires assuming TCP fragmentation. A JSON payload might be sliced precisely in the middle of a string token `{"name": "SlabF|` ... `lux"}`.

- **State Suspend**: The parser instantly yields `parser_status::INCOMPLETE`, saving its exact transition phase to the `json_frame`.
- **State Resume**: Upon receiving the subsequent TCP burst, the DFA resumes the execution at the exact stalled instruction.

While traversing massive JSON strings during the `L_IN_STRING` state, the parser engages the `json_simd_utils::find_string_delimiter` to scan up to 64 bytes per clock cycle, searching for closing quotes or escaped control characters using pure AVX hardware masks.

## 5. Security & Malformity Rejection
- **Infinite Nesting Prevention**: The parser's execution is bounded strictly by the pre-allocated `max_tokens` capacity provided by the host thread. If an adversary submits infinitely nested objects `{{{{{...`, the parser halts harmlessly upon reaching the limit.
- **Strict RFC 8259 Compliance**: Disallows unescaped control characters in strings, verifies trailing commas, and enforces valid floating-point numeric topologies.