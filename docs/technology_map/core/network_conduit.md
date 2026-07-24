# Network Conduit & Gateway (`network_conduit.hpp`)

## Architekturális Koncepció
Standard Socket Egress Fallback. Ez a modul felelős az események (raw TCP keretek vagy protokoll-csomagok) natív kernel szoftver-stacket használó hálózati kártyára küldéséért.

## Szinkronizációs Modell
A `standard_egress_gateway` egy Cél Conduitot (pl. `spsc_conduit`) olvas `pop_batch` hívással (AVX/SIMD burst), majd a kinyert mutatókon lévő adatokat szabványos `send(MSG_NOSIGNAL)` hívással juttatja le a kernelhez.
Beépített Backpressure védelem: `EAGAIN` / `EWOULDBLOCK` esetén a mutatókat veszteségmentesen (O(1)) visszahelyezi a csatornába (`revert_batch`), majd megszakítja az iterációt (Zero Spin).