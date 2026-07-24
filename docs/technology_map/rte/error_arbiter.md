# Legacy Error Arbiter (`error_arbiter.hpp`)

## Architekturális Koncepció
Zéró-allokációs, determinisztikus hibanapló és arbitrátor MPSC vagy SPSC forgatókönyvekre. Egy statikus (heap alloc mentes) gyűrűpufferben (Ring Buffer) vezeti a fix 32-bájtos hiba-rekordokat (`error_record`), hogy azok ne törjék meg a pipeline teljesítményét.

## Funkcionalitás
- Két mutató (write cursor, cached read cursor) biztosítja az inter-thread szinkronizációt.
- Automatikus `panic_flag_` élesítés és Escalation Policy (Out-of-band Callback) végrehajtás `critical` vagy `fatal` besorolás esetén.
- OOM-védett: ha a gyűrű megtelt, "Teleological Agnosia" lép érvénybe (eldobja a naplóbejegyzést a haladás garantálása érdekében).