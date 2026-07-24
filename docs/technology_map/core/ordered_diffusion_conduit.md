# Ordered Diffusion Conduit (`ordered_diffusion_conduit.hpp`)

## Architekturális Koncepció
Lock-Free, SIMD-gyorsított szigorúan rendezett MPMC Mátrix. A klasszikus `fetch_add` tiket-alapú sorok helyett itt a szálak egy lustán (lazy) léptetett kurzort használnak referenciaként, majd onnan AVX2 (`_mm256_movemask_epi8`) Sweep-pel szkennelik a memóriát egyszerre 32 cellás blokkokban.

## Szinkronizációs Modell
- **Head-of-Line Bypass:** A SIMD pásztázás lehetővé teszi, hogy ha a 0. cella írása alatt a szál elakad, egy másik szál ne blokkolódjon, hanem azonnal az 1. cellát azonosítsa szabadként. 
- **FIFO Szigorúság:** A bitmaszk levágása garantálja, hogy a szálak fizikailag nem kaphatnak régebbi sorszámot a már kifuttatott hullámfrontnál, így fenntartja a tökéletes esemény-időrendet (Chronological Ordering).