# Asymmetric Dispersion Matrix Queue (`asymmetric_dispersion_queue.hpp`)

## 1. Az Aszimmetria Hardveres Kihasználása
A legtöbb MPSC (Multi-Producer Single-Consumer) queue implementáció indokolatlanul bünteti a Consumer szálat azzal, hogy több-olvasós (Multi-Consumer) szinkronizációs primitíveket használ, például folyamatosan frissített `tail` indexeket vagy atomi CAS (Compare-And-Swap) utasításokat. A `asymmetric_dispersion_queue` felismeri az MPSC felállás strukturális aszimmetriáját, és a Consumer oldalról minden drága hardveres utasítást száműz.

### Architekturális Koncepció
Világelső teljesítményű MPSC (Multi-Producer Single-Consumer) queue. Kihasználja a struktúra eleve aszimmetrikus mivoltát.

## 2. Producer: Diszperzív Térbeli Pásztázás
A Producerek a mátrixon vízszintesen, L1-cache vonalakba rendezett sorokon (Rows) pásztáznak. Szinkronizációjuk a `compare_exchange_strong` műveletre épül, amely garantálja, hogy egy üres (`VACUUM`) cellát pontosan egy szál foglalhat el (`PLASMA`). 
Mivel a kezdő soraik szál-szinten szét vannak szórva (Dispersion), a Producerek egymástól fizikailag izolált memóriaterületeket írnak, elkerülve a False Sharing-et a több processzoros rendszereken. A push művelet Wait-Free: nincsenek CAS spinloop-ok.

### Szinkronizációs Modell
- **Producer:** Vízszintes pásztázással (`compare_exchange_strong`) keres egy szabad `nullptr` slotot, ha sikertelen, továbblép a következő sorra. Wait-Free.
- **Consumer:** Mivel a Producerek sosem írják felül a már meglévő `T*` értékeket, a Consumer **Zéró-CAS** módszerrel operál. Sima `load(acquire)`-rel kiolvassa, majd `store(release)`-zel nullázza a cellát. Lock-Free.

Eliminálja a Consumer oldalról az összes atomi Compare-And-Swap utasítást, drasztikusan növelve az aszimmetrikus csatornák szalagsebességét.

## 3. Consumer: Zéró-CAS (Lock-Free) Extrakció
A queue valódi áttörését a Consumer olvasási ciklusa adja. Mivel a Producerek *kizárólag* `nullptr`-t írnak felül `T*`-ra, de sosem írnak felül meglévő `T*`-t, a Consumer axiomatikusan biztonságban van, ha egy cellát `PLASMA` állapotban talál. Nincs szüksége `LOCK CMPXCHG` (CAS) utasításra a memóriacella "lezárásához".

Az olvasás fizikája:
1. **Acquire Load:** A Consumer egy egyszerű `load(std::memory_order_acquire)` utasítással lekéri a pointert (x86-on ez egy sima `MOV` utasítás, zéró barrier overheaddel).
2. **Release Store:** Miután kinyerte a pointert, a Consumer a cellát egy `store(nullptr, std::memory_order_release)` utasítással visszaminősíti `VACUUM` állapotba. Ezen a ponton a Producerek ismét láthatják üresként.

Ez a Zéró-CAS modell meggátolja, hogy a Consumer szál lezárja a CPU memóriabuszát, ami aszimmetrikus forgatókönyvekben (pl. hálózati csomagok feldolgozása egyetlen logika által) hatalmas sávszélesség-többletet (throughput) eredményez.

## 4. Elágazásmentes (Branchless) Rotáció
Az indexek határon történő átfordulása a klasszikus megoldásokban modulo operátorral (`col % COLS`) vagy feltételes ugrásokkal (`if (col >= COLS) col = 0;`) történik.
Az ADM modell predikációs (CMOV barát) szubtrakciót alkalmaz:
```cpp
std::size_t col = (c_col_ + c);
if (col >= COLS) col -= COLS;
```
Ez garantálja, hogy a rotáció nem akasztja meg az ALU Integer osztó (Divider) áramköreit.