# Event Gateway (`event_gateway.hpp`)

## Architekturális Koncepció
Általános célú adatáramlás-szabályozó osztály, amely hidat képez a Conduit memóriacsatornák és a végső feldolgozó (Sink / Pipeline) logika között.

## API és Mechanika
- `drain<EventType, BatchSize>`: Egy teljes SIMD batch (alapértelmezetten 16 pointer) kinyerése a buszról, feldolgozása a paraméterként kapott lambdával/függvénnyel, majd O(1) atomi tranzakcióval történő **kollektív újrahasznosítás** (`release_batch`) a pool felé.
- `publish`: Elrejtett RAII allokáció és push operáció, amely dedikáltan O(1) idő alatt tolja be a poolból képzett friss memóriát az adott Conduit struktúrába.