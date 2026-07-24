# Orthogonal Error Arbiter (`orthogonal_error_arbiter.hpp`)

## Architekturális Koncepció
Világelső, kivétel- és elágazásmentes, O(1) komplexitású **Ortogonális Szubszumpciós Mező (Orthogonal Subsumption Field)**. Megszünteti a hibanaplók, enumok és sorbaállított események overheadjét.

## Szinkronizációs Modell
- **Prím-faktorizált Topológia (Gödel Numbering):** A hibákat egyedileg prímszámok szorzataként definiálja. Reláció és alárendeltség (hierarchia) $O(1)$ oszthatósági modullóval (`%`) történik, zéró `if` ugrás.
- **Wait-Free Subsumption:** A memóriamező egy `std::atomic<uint64_t>` tömb. A beírandó hiba Knuth Hash-sel dedikált pozícióra esik. A hiba rögzítése 1 db `store`, OOM és Backpressure nélkül felülírja (Subsume) az azonos kategóriájú előző hibát.
- **Destruktív Olvasás:** A Consumer a cellát `exchange(0)`-val kérdezi le. Zéró-CAS, zéró Sequence Counter.