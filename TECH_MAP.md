# SLABFLUX SOFTWARE ENGINE - TECHNOLOGY MAP

Ez a dokumentum a SlabFlux rendszer architekturális és technológiai térképét (Tech Map) tartalmazza, amely a megszokott programozási paradigmákon túllépve, fizikai, topológiai és axiomatikus elvekre építkezik.

## 1. Ontological Compute Subsystem (Ontológiai Számítási Rendszer)
A SIMD vektorizáció és állapotkezelés hardver-intrinzikek helyett tiszta matematikai alapokon.

- **Axiomatic Vector Lane (`axiomatic_vector.hpp`)**: Hardver-wrapper helyett egy dimenzionális sokaság (manifold), ahol az adat (Data) és a maszk (Substance Field) szigorú téridő-szövetet alkot.
- **Gödel-számozott Hibarács (Error Lattice)**: A hibák enumok helyett prímszám-faktorizáción alapuló részben rendezett halmazt (Poset) alkotnak. A *Lattice Join* (LCM) és *Lattice Meet* (GCD) biztosítja a hibák fúzióját.
- **Self-Diagnosing Type System (`Validated<T>`)**: Monadikus burkoló (Monad), amely izolálja a "mérgezett" állapotokat, és fizikai/fordítási szintű pánikot (Axiomatic Collapse) okoz a korrupt adatok kinyerésekor.

## 2. Quintipartite Hardware Defense (Ötrétegű Hardveres Védelem)
Az OS-szintű memóriavédelem (MMU) megkerülése miatt a SlabFlux a szilícium szintjén védi az integritást.

- **Symplectic Resonance Fencing (`sovereign_signal.hpp`)**: Geometriai feszültségen alapuló adatintegritás, amely a korrupt adatokat 3 CPU ciklus alatt vákuummá alakítja (Topological Vaporization) az AVX-512 FMA egységein keresztül.
- **ILP Shadowed Executor (`ilp_shadowed_executor.hpp`)**: Az üzleti logika (FMA, Port 0/1) mögött a fizikai "árnyékban" (Port 5, VPCONFLICTD) futó konfliktusdetektálás (Autologous Isomorphism).
- **Minkowski Data Lattice (`mdl_state_array.hpp`)**: A memóriában lévő adatokat az aktuális logikai idő-kúphoz (Light-Cone) köti, így a rohadó bitek hardveresen automatikusan nullázódnak (Lorentz Subsumption).
- **Superscalar Replay Validator (`replay_validator.hpp`)**: Háromutas interleaving CRC32 az ALU-k maximális fizikai telítésére.
- **Numerical Sanitizer (`numerical_sanitizer.hpp`)**: Differenciális tisztítás és Neighbor-Weighted SIMD-gyűrűs interpoláció NaN/Inf anomáliák ellen.

## 3. High-Fidelity Concurrency & Memory (Konkurencia és Memória)
Zéró elágazás, zéró rendszerhívás, hardveres L3 izoláció.

- **Conduits & Pools (`spsc_pool`, `mpmc_conduit`, stb.)**: Lock-free, wait-free allokátorok és csatornák.
- **Sharded MPMC Matrix (`mpmc_sharded_conduit.hpp`)**: A szál-specifikus szilánkosítás (sharding) teljesen eliminálja a MESI protokollból eredő Cache-Line pattogást (False Sharing).
- **Pinned Allocators (`pinned_allocator_isolated.hpp`)**: Kifejezetten a DMA hálózati kártyákhoz és io_uring-hez (HugePage, MAP_LOCKED) igazított, térbelileg szeparált memóriakazetták.
- **Orthogonal Manifold / Cross-Orthogonal Queue (`orthogonal_manifold.hpp`, `cross_orthogonal_queue.hpp`)**: Zéró `head`/`tail` pointeres MPMC architektúrák, amelyek egy kétdimenziós ortogonális rácson minimalizálják a producer-consumer contentiont.
- **Asymmetric Dispersion Queue (`asymmetric_dispersion_queue.hpp`)**: Zéró CAS-ciklusú (Consumer oldalon teljesen zéró-CAS) MPSC architektúra térbeli diszperzióval.
- **Pendulum SPSC Conduit (`pendulum_spsc_conduit.hpp`)**: Boustrophedon (inga) bejárású SPSC sor, amely kiküszöböli a körkörös ugrások okozta L1/L2 prefetcher miss-eket és TLB elakadásokat.

## 4. Hardware Sovereignty (Hardveres Szuverenitás)
A Compute magok teljes leválasztása a Linux Kernel ütemezőjéről.

- **Sovereign Observer (`timing_invariant.hpp`)**: Explicit cache-vonal szigeteléssel és `_mm_prefetch` alapú optimista olvasással operáló telemetria.
- **Thermal Soak & SMI Monitor (`thermal_soak.hpp`, `smi_monitor.hpp`)**: System Management Interrupt (SMI) észlelés és CPU hőmérséklet-stabilizálás a frekvencia-visszalépések (Throttling) ellen.
- **Uncore Lock & Topology Enforcer (`uncore_lock.hpp`, `topology_scanner.hpp`)**: MSR alapú hálózat/L3 frekvencia maximalizálás és natív CPUID NUMA-L3 cache validáció.

## 5. 7D Matrix Routing & Pipeline
Sokdimenziós, öröklődésen alapuló futásidejű Dispatching, zéró polimorfizmussal.

- **Branchless Dispatch Unroller (`dispatch_unroller.hpp`)**: Esemény DAG és Kezelő DAG kompozíciójának C++20 template unrolling-ja "Straight Line" gépi kóddá.
- **Inverse Priority Sort (`inverse_priority.hpp`)**: Családfa-alapú lexikografikus és prioritás-alapú topológiai sorrend.
- **Ephemeral Context Vault (`context_vault.hpp`)**: Az Esemény útvonalának megfelelően L1 stacken allokált ideiglenes kontextus tartály O(1) rezolúcióval.
- **Phase Engine (`phase_engine.hpp`)**: Szignatúra és Osztály-szintű fázis detektálás mutual exclusivity ellenőrzéssel.

## 6. Zero-Copy I/O & Networking
- **io_uring & AF_XDP Bypasses**: O_DIRECT fájl persistencia (Durable Journal) és Kernel-Bypass hálózati réteg (Ingress/Egress).
- **Chicago Gateway Demuxer (`demux_gateway.hpp`)**: Csomag-azonosító (Packet ID) alapján történő O(1) Dispatching a HFT hálózati betáplálásból.

## 7. Orthogonal Error Arbitration (Ortogonális Hiba-Arbitrázs)
Kivétel- és elágazásmentes, O(1) komplexitású hardver-közeli hibakezelés.

- **Orthogonal Subsumption Field (`orthogonal_error_arbiter.hpp`)**: A hibákat fizikai tenzorokként modellezi egy szubszumpciós mezőben, ami várólisták (queue) nélkül, folyamatos O(1) írást tesz lehetővé, kiküszöbölve a Backpressure és OOM veszélyeit még extrém hibaráta esetén is. Gödel-számozás (prím-faktorizáció) biztosítja az elágazásmentes hierarchia-relációkat.