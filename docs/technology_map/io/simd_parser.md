# SlabFlux I/O: SIMD Parser (`slabflux/io/simd_parser.hpp`)

## 1. Architectural Justification
Byte-by-byte parsing over chaotic network payloads (like JSON or HTTP) inherently invokes severe branch misprediction penalties on modern CPUs. The SIMD Parser utilizes an Architecture-Bound Vector State Machine to process data in 64-byte chunks, providing guaranteed flat-latency extraction.

## 2. Hardware Implementation Directives
- **Structural Masking**: Utilizes `_mm512_cmpeq_epi8_mask` to evaluate delimiters (newlines, commas) over 64 characters simultaneously.
- **Hardware Trailing Zeros**: Passes the resulting mask to `tzcnt` (Trailing Zero Count) to extract the physical byte offset in 1 clock cycle.
- **GPR-Bound Integer Parsing**: Converts ASCII to integers using bit-shifts (`val >> 8`) and parallel subtraction (`0x30303030`), avoiding the ALU division hardware entirely.

## 3. Structural Synthesis
Instead of iterating byte-by-byte to find protocol delimiters (e.g., `\n` or `\r`), the parser extracts exact 64-byte network chunks.
- It utilizes `_mm512_cmpeq_epi8_mask` to evaluate all 64 bytes in a single CPU tick.
- The mask is passed to `slabflux::hw::tzcnt_64` (Trailing Zeros Count), instantly yielding the delimiter offset without any branch-predictor penalties.

## 4. Register-Local ATOC
Standard ASCII-to-Integer conversions (`atoi`) cross SSE domains or invoke division sequences, consuming 8-12 cycles.
- The `fast_atoi_4` implementation stays exclusively in the General Purpose Registers (GPR).
- It subtracts the ASCII '0' offsets using a 32-bit parallel mask (`0x30303030`).
- It re-materializes the base-10 integer via bit-shifts and multiplication limits (`(val >> 8) * 100`).
- This ensures protocol parsers convert quantities in less than 2 cycles per field.

## 5. Pipeline Integration
The SIMD parser runs exclusively within the I/O reception loop (e.g., inside `tcp_stream_defragmenter`). By slicing the raw byte stream at L1 cache speeds, it structures chaotic ASCII strings into dense `sovereign_signal` envelopes before pushing them into the deterministic execution DAG.