# SlabFlux Compute: Reproducible Random (`reproducible_random.hpp`)

## 1. Architectural Overview
Stochastic AI modeling and deterministic load balancers require vast amounts of entropy. Using the standard OS `/dev/urandom` or `std::mt19937` breaks determinism, making system replay and fault reconstruction mathematically impossible.

The SlabFlux `reproducible_random` implements the proprietary **Orbital Cascade (OC-64)** PRNG, delivering cryptographically resilient, 100% replayable entropy at hardware speed.

## 2. The Orbital Cascade Algorithm
Unlike PCG, SplitMix, or Xoshiro families, the OC-64 relies solely on strictly bijective (reversible) bitwise arithmetic and modular additions to guarantee absolutely uniform output distribution.

- **Extended Phase Accumulator (Weyl Sequence)**: The `long_entropy_rng` extends the core accumulator into a native 128-bit `unsigned __int128` integer. This physically ensures that the mathematical period of the generator cannot overlap for at least $2^{128}$ iterations.
- **Turbulence Register Matrix**: It mathematically entangles the strict 128-bit clock with a 64-bit turbulence state using an odd-parity multiplicative constant (`9817234650192837411ULL`), followed by deterministic circular shifts (`std::rotl`/`std::rotr`) that compile down to single-cycle x86-64 instructions.

## 3. Seed Avalanching
If two execution pipelines are spawned with adjacent Logical Sequence Numbers (e.g., LSN 100 and LSN 101), generic PRNGs exhibit catastrophic spatial correlation for the first few thousand cycles.

The OC-64 executes a pre-scrambler `mix_seed()` function at boot:
- It forces the sequential seed through a cascading XOR/Multiplication bit-mixer.
- It then executes 3 full "warmup" cycles on the internal state.
- This enforces an immediate, violent avalanche, ensuring that sequentially seeded pipelines generate completely orthogonal noise floors from tick zero.

## 4. Hardware SMT Yielding
The generator offers two tiers:
- `deterministic_rng`: The default 64-bit bounded generator optimized for absolute maximum entropy extraction per tick.
- `long_entropy_rng`: The 128-bit generator designed for immense, non-repeating physics and AI simulations.