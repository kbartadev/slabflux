# Causal Entity (`causal_entity.hpp`)

## Architekturális Koncepció
Szuverén állapotgép és entitás-modell, amely egyetlen folytonos 64-bájtos (vagy többszöröse) memóriablokkban tárol térbeli, időbeli és logikai topológiát.

## Dimenziók
- **Tér / Topológia:** Bitmask-alapú Ownership leképezés (pl. Logic CPU, NLP GPU).
- **Idő (TAI):** Determinisztikus, Hardveresen kalibrált TAI (International Atomic Time) időbélyegző, leap-second (szökőmásodperc) okozta elakadások nélkül.
- **Szuverén Logika:** Egy statikusan becsomagolt C++ koncepció (`SovereignStateMachine`), amely a `pulse` hívásakor zéró v-table (virtuális tábla) call overheaddel állítja elő az állapotváltást.