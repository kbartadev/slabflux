# Deterministic Exit (`exit.hpp`)

## Architekturális Koncepció
Determinisztikus, aszinkron szignálbiztos (Async-Signal-Safe) rendszerleállító procedúra.

## Megvalósítás
A klasszikus C/C++ I/O zárakat megkerülve (nincs `std::cout` lock) egyenesen az `STDOUT_FILENO`-ra ír a libc `write` függvényével. Szabályozott sorrendben állítja le az alrendszereket:
1. A napló (Durable Journal) konzisztens lezárása (`seal()`).
2. A fizikai memóriatérkép bit-pontos mentése (Snapshot) az Örökös Snapshot Engine-nel.