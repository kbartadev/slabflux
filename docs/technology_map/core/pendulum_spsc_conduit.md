# Pendulum SPSC Conduit (`pendulum_spsc_conduit.hpp`)

## 1. A Gyűrűpuffer (Ring Buffer) Hardveres Korlátai
Az ipari szabvány Single-Producer Single-Consumer (SPSC) megoldások 99%-a gyűrűpuffert használ körkörös indexeléssel (`index & (Capacity - 1)`). Bár algoritmikusan egyszerű, a hardver számára van egy fatális hibája: a **körkörös szakadás (Wrap-around Discontinuity)**.
A modern processzorok (Intel/AMD) L1/L2 Cache Prefetcher áramkörei az adatokat szigorú lineáris mintázatok (Strides) alapján töltik be előre a memóriából. Amikor a gyűrűpuffer a tömb legvégére ér és visszaugrik a `0`-ás indexre, a hardveres prefetcher "becsapódik" (megtörik a lendülete), ami cache miss-t, TLB (Translation Lookaside Buffer) elakadást és pipeline buborékot okoz.

### Architekturális Koncepció
Inga-Hullámfront (Boustrophedon) bejárású SPSC queue. Megoldja a klasszikus ring bufferek körkörös ugrása (wrap-around) okozta Branch Predictor és Spatial Prefetcher töréseket. 
A kurzor nem ugrik nullára a végén, hanem megfordul ($+1$ lépésközről $-1$-re vált), és fizikailag visszafelé pásztázza a memóriát.

### Szinkronizációs Modell
Zéró közös metaadat. Nincs megosztott `head` és `tail` index, a szinkronizáció magán az adatmezőn (`std::atomic<T*>`) oszlik el.
- Zéró-CAS `load-acquire` / `store-release` modell mindkét oldalon.
- Tökéletes hardveres prefetching a 100%-os folytonos lineáris bejárás miatt.

## 2. Inga-Hullámfront (Boustrophedon) Bejárás
A `pendulum_spsc_conduit` elveti a kört, és a memóriát egy **véges, lineáris szakaszként** értelmezi.
A szálak elindulnak a `0` indextől az `N-1` indexig (stride: $+1$). Amikor elérik a tömb végét, ahelyett, hogy a nullára ugranának, **megfordulnak**, és fizikailag visszafelé haladnak az $(N-2) \to 0$ útvonalon (stride: $-1$). Ezt ismétlik végtelenségig, akárcsak egy inga (Pendulum) mozgása.

## 3. Memóriamodell és Izoláció
A struktúra teljes mértékben eliminálja a közös metaadatokat:
- Nincsenek globális `head` és `tail` atomi számlálók.
- A Producer és Consumer egy kizárólagosan privát (lokális) állapot-struktúrát (index + stride) használ, melyek külön L1 Cache-vonalakra (`alignas(64)`) vannak szigetelve. Sosem olvassák egymás kurzor pozícióját.
- A szinkronizáció 100%-ban magában a memóriahálóban történik, a cellák ontológiai (`VACUUM` vs `PLASMA`) állapotán keresztül, `load(acquire)` és `store(release)` memóriarendezésekkel. Zéró-CAS operáció, mindkét oldalon Wait-Free garanciával.

## 4. Teljesítménybeli Hatások a Processzorban
1. **Prefetcher Optimizáció:** Mivel a hozzáférés irányváltása teljesen organikus, a Spatial Prefetcher képes tökéletesen anticipálni a következő memóriablokkot (az Intel L2 Prefetcherek natívan képesek követni az "előre, majd hátra" mintázatokat rövid tanulási fázis után).
2. **BTB (Branch Target Buffer) Predikció:** A határ ellenőrzése egy feltételes ág:
   ```cpp
   if (index == Capacity) { stride = -1; ... }
   ```
   Mivel a fordulás determinisztikusan pontosan minden `N`-edik iterációban következik be, a CPU Branch Predictor tökéletesen leképezi a mintát, garantálva a közel 100%-os elágazás-előrejelzési (Branch Prediction) pontosságot.
3. **Kapacitáskezelés (Bound Check):** A sor megtelésének (Full) és kiürülésének (Empty) esetei az adatmezőn történő ráfutással vannak lekezelve. Mivel mindkét szál azonos pályán oszcillál, a Producer fizikailag nem előzheti meg a Consumert a rácson anélkül, hogy bele ne ütközne a le nem olvasott `PLASMA` blokkokba.