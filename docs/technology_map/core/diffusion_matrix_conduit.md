# Diffusion Matrix Conduit (`diffusion_matrix_conduit.hpp`)

## Architekturális Koncepció
Statically-Indexed AVX2/AVX-512 Swept Matrix. Kialakítása hasonló a rendezett változathoz, ám itt a szálak dinamikus útvonal (routing) helyett specifikus statikus sávokat (Lanes) vehetnek célba, optimális hardcoded kereszt-mag topológiák kialakításához.

## Memóriamodell
Egy 1-bájtos kompresszált állapotmátrix (State Matrix) felelős a cellák állapotáért (Vákuum, Rezervált, Kész, Kinyert). Ez a struktúra memóriafolytonosan közvetlenül bekerül a YMM/ZMM regiszterekbe. Az L1 cache miss minimális, mivel nincsenek 8-bájtos elszórt szekvencia-jegyek (Sequence Tickets). 
Erős HugePage + MLOCK architektúrát épít fel POSIX alatt.