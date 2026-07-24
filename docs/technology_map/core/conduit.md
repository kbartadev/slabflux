# Conduit Concept (`conduit.hpp`)

## Architekturális Koncepció
A SlabFlux adatáramlásának fundamentális absztrakciós szerződése (C++20 Concept). Minden szállítási rétegnek (SPSC, MPMC, Sharded) ehhez az API-hoz kell igazodnia.

## Metódusok
- `push` / `try_push` / `on_raw_frame`: Ingress útvonalak a PipelineLogic direkt integrációjához.
- `pop` / `try_pop`: Egress adatkinyerés.
- `pop_batch` / `push_batch`: SIMD-gyorsított (AVX-512) pointer-mozgatás az L1 memóriasávszélesség maximalizálásához.

## Alias
- `conduit<T, Capacity, Lanes>`: Fejlesztői alapértelmezett alias, amely a biztonságos, skálázódó MPMC (`mpmc_conduit`) struktúrát referálja.