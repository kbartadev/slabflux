# Buffer Flush (`buffer_flush.hpp`)

## Architekturális Koncepció
Extrém alacsony szintű szinkronizációs primitív a Processzor Írási Puffereinek (Write-Combining / Line-Fill Buffers - LFB) azonnali, szoftveresen kényszerített kiürítésére.

## Implementáció
A hagyományos `_mm_sfence()` utasításon túl egy `lock; addq $0` trükköt használ egy dummy változón. Ez az x86/x64 architektúrán olyan teljes memória-sorosítást (total serializing point) kényszerít ki, amely garantáltan kifuttatja az L1 cache alatti aszinkron pufferekből az adatot a fizikai memóriabuszig.