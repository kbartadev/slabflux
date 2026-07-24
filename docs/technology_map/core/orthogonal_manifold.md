# Orthogonal Manifold (`orthogonal_manifold.hpp`)

## Architekturális Koncepció
Ortogonális Dimenzió-Útválasztású MPMC Queue. Eliminálja a megosztott `head`/`tail` mutatókat, és a várakozó szálakat egy kétdimenziós rácson szórja szét. A mátrix sorai pontosan egy-egy L1 Cache vonal méretéhez (64 bájt) vannak igazítva.

## 1. Topológiai Dimenzió-Útválasztás (Dimensional Routing)
A klasszikus MPMC (Multi-Producer Multi-Consumer) adatszerkezetek legnagyobb problémája a topológiai egydimenziósság. Mivel minden szál egy globális `head` vagy `tail` atomi számlálón verseng, a CPU magok közötti interconnect busz (QPI/Infinity Fabric) túlterhelődik az RFO (Read-For-Ownership) cache-vonal érvénytelenítési viharoktól. 

## Szinkronizációs Modell
- **Producer (Wait-Free Bounded):** Thread-seed alapján egy saját soron (Cache Line) indul, és vízszintesen pásztáz. Nincs producer-producer contention a soron belül.
- **Consumer (Lock-Free):** Függőlegesen pásztáz, metszve a cache-vonalakat. Egy consumer és egy producer maximálisan egyetlen mátrix-cellán találkozhat.
- **Állapotgép:** Kétállapotú (Vákuum/Plazma). Nincsenek epoch- vagy sequence-számlálók, az ABA-biztonság a szigorú egyirányú állapotátmenetekből és az újrahasznosítási fáziskésésből adódik.

Az `orthogonal_manifold` elveti a lineáris láncot, és a memóriát egy **kétdimenziós mátrixként (rácsként)** szervezi meg. 
- **Producerek** vízszintesen, sorfolytonosan (row-major) pásztáznak.
- **Consumerek** függőlegesen, oszlopfolytonosan (column-major) haladnak.
- **Metszéspont:** A geometria matematikailag garantálja, hogy egy Producer és egy Consumer pontosan egyetlen elemén keresztezi egymást egy bejárási ciklus alatt, radikálisan eloszlatva a fizikai versengést (Contention Scattering).

## 2. Bipartit (Kétállapotú) Memóriamodell
Nincsenek "Ticket"-ek, szekvencia-számlálók vagy ABA-verziócímkék (epoch tags). A mátrix sejtjei (`std::atomic<T*>`) tisztán ontológiai állapotgépet követnek:
- `VACUUM`: A cella üres (`nullptr`).
- `PLASMA`: A cella érvényes adatot tartalmaz (`T*`).

A szinkronizáció biztonságát a tranzíciók szigorú egyirányúsága adja: Producerek kizárólag `VACUUM -> PLASMA`, míg Consumerek kizárólag `PLASMA -> VACUUM` átmenetet válthatnak ki atomi `compare_exchange_strong` segítségével. Mivel nincs numerikus számláló, ami túlcsordulhatna, a klasszikus ABA probléma értelmezhetetlen.

## 3. L1 Cache Geometria és MESI protokoll
A rács dimenziói nem véletlenszerűek. A mátrix egyetlen sora (Row) pontosan úgy van dimenzionálva, hogy kitöltsön egy fizikai L1 Cache vonalat (`alignas(64)` vagy `std::hardware_destructive_interference_size`). 
Mivel egy Producer vízszintesen pásztázza a sort, a memóriavezérlő a teljes cache-vonalat `Exclusive` (E) vagy `Modified` (M) állapotba emeli az adott CPU maghoz. Ezzel a Producer várhatóan $N$ iteráción keresztül anélkül dolgozhat, hogy a szomszédos szálak invalidálnák a cache-vonalát (False Sharing zéróra redukálva).

## 4. Szinkronizációs Dinamika
- **Wait-Free Bounded Push**: A Producer sosem lép CAS-pörgésbe (spin-loop). Ha a cella `PLASMA` állapotú (vagy valaki megelőzte a CAS alatt), `O(1)` időben fellépteti a belső, thread-local kurzorát, és megy tovább. Ha a rács tele van, azonnal visszatér.
- **Lock-Free Pop**: A Consumer függőlegesen pásztáz. Nincs üres-várakozás (spin-wait) a `nullptr`-eken.