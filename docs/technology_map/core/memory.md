# Memory Structures (`memory.hpp`)

## Architekturális Koncepció
Alapszintű memóriaabsztrakciók és C++ mutató-wrapper típusok definíciója a zéró-elágazásos memória útválasztáshoz.

## Objektumok
- `event_ptr<T>`: Alias az `event_guard` RAII pointerre unmanaged (`null_pool_base`) élettartamhoz.
- `tagged_pointer`: Zéró-overhead 8-bájtos struktúra hálózati csomagokhoz. Felső 16 bitet azonosító címkeként (Tag), alsó 48 bitet memóriacímként használ. A memóriacím visszanyerésekor egy O(1) bitművelet (`int64_t` shift) végzi el a Canonical Address Sign-Extension-t, kiküszöbölve a kernel/MMAP negatív mutatókból eredő címhibákat.