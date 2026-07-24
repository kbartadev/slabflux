# HugePage Allocator (`hugepage_allocator.hpp`)

## Architekturális Koncepció
Fizikai szintű rezidenciát garantáló memóriaallokátor (Zero-copy). Az `allocate_huge_pinned` egy direkt `mmap` hívással dolgozik, a memóriát 2MB-os Page (Oldal) határokhoz igazítva.

## Specifikáció
A `MAP_HUGETLB` és `MAP_LOCKED` flag-ek garantálják, hogy a memória sosem kerül Swap-be, és drasztikusan csökkentik a TLB (Translation Lookaside Buffer) Miss arányát a hardveren. Integrált `libnuma` kötéssel a lefoglalt fizikai terület ahhoz a NUMA node-hoz kötődik, ahol a hívó szál fut, megakadályozva a QPI/UPI kereszt-socket busz ping-pongozást.