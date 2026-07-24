# Axiomatikus Vektor Architektúra (Gödel-Lattice SIMD)

A **SlabFlux Ontological Compute Subsystem** egy világelső megközelítése a SIMD és párhuzamos adatfeldolgozásnak. Ahelyett, hogy a hardver (AVX/NEON/SVE) regisztereit csomagolnánk egy vékony C++ API-ba (ahogy a legtöbb matematikai vagy SIMD könyvtár teszi), egy **felülről lefelé építkező, tiszta matematikai axiómákon alapuló ontológiai modellt** alkalmazunk.

Ebben a modellben a vektorizáció csupán egy következménye a szigorú topológiai és algebrai szabályoknak.

---

## 1. Az Axiomatikus Rendszer (Axioms of the Ontological Vector)

A `VectorLane<T, N>` osztály viselkedését 5 megmásíthatatlan axióma definiálja. Ezekből vezetjük le a rendszer teljes működését és hibamodelljét.

### I. Axiom of Dimensionality (Topológia)
Egy `VectorLane` nem egy egyszerű adattömb (array), hanem egy $N$-dimenziós diszkrét sokaság (manifold). A határai szigorúak ($\partial M = \emptyset$). A dimenziószámnak ($N$) fordítási időben a pozitív egész számok ($\mathbb{Z}^+$) halmazába kell tartoznia.

### II. Axiom of Dual-Substance (Relációk)
Minden csomópont a sokaságon belül két állapot szuperpozíciója: a szubsztancia/maszk $\mu \in \{0,1\}$, és a tényleges adat $v \in T$. Relációjuk abszolút: ha a szubsztancia hiányzik ($\mu = 0$), az adatnak kötelezően a típus algebrai üres elemének ($T\{\}$) kell lennie ("Tisztátalan Nullák" tiltása).

### III. Axiom of the Continuum (Tiltott Állapotok)
A szubsztancia-mező nem tartalmazhat vákuumot két létező adat között. A maszknak balról jobbra szigorúan monoton csökkenőnek kell lennie (pl. az `1, 1, 1, 0` egy érvényes folytonos téridő, de az `1, 0, 1, 0` egy tiltott **Topológiai Szakadás**).

### IV. Axiom of the Prime Poset (Error-Lattice)
A hibaállapotok nem izolált enum kódok, hanem egy Részben Rendezett Halmaz (Poset / Lattice) csomópontjai. Az összevonásokat és alá-fölérendeltségeket a Gödel-számozás szabályai vezérlik (lásd később).

### V. Axiom of Diagnostic Closure (Valid Állapotterek)
A vektorokon végzett műveletek (pl. összeadás) sosem adhatnak vissza nyers vektort. Minden transzformáció eredménye egy zárt `Validated<T>` Monád, amely megakadályozza a mérgezett (korrupt) adatok véletlen kinyerését.

---

## 2. A Gödel-számozott Hibarács (Error Lattice)

A megszokott C++ kivételek (exceptions) vagy egyszerű enum hibakódok helyett a rendszer a hibákat prímszámokként (Prime Factors) és azok szorzataként képezi le.

- **Lattice Node-ok (Prímek):**
  - `NoError = 1`
  - `InvalidStateBase = 2`
  - `LaneMismatchBase = 3`
  - `ContinuumFaultBase = 5`

- **Komplex Hibák (Szorzatok):**
  - `InvalidMask = 10` (InvalidStateBase * ContinuumFaultBase)
  - `TopologyViolation = 30` (InvalidMask * LaneCountMismatch)

- **Lattice Operations:**
  - **Lattice Join (Összevonás):** A Legkisebb Közös Többszörös (LCM) képzése. Ha két különböző hiba történik, a szorzatuk egy új, magasabb rendű hibát reprezentál a rácsban.
  - **Lattice Meet (Finomítás):** A Legnagyobb Közös Osztó (GCD) képzése.
  - **Relációvizsgálat (`Has`):** Oszthatósági vizsgálat (`state % error == 0`).

Ez a modell lehetővé teszi, hogy egyetlen `uint64_t` értékben végtelen kombinációjú eseménytörténetet és hibaláncolatot kódoljunk zéró elágazással.

---

## 3. Self-Diagnosing Type System (`Validated<T>`)

Minden algebrai művelet és inicializáció visszatérési értéke a `Validated<VectorLane<T, N>>`.

Ez a típus:
1. **Hordozza a Bizonyítékot (Proof-carrying code):** Tartalmazza a generált Gödel-hibarács azonosítót.
2. **Izolálja a Kvantum-összeomlást:** Tilos a nyers adat kinyerése anélkül, hogy a felhasználó explicit ne hívná meg az `extract_or_panic()` metódust.
3. **Pánik:** Ha a lánc bármelyik pontján topológiai szakadás vagy dimenzió-ütközés történt, az `extract_or_panic()` C++20 consteval fázisban fordítási hibát generál, futásidőben pedig azonnali architekturális összeomlást (panic) vált ki, így lehetetlen a hibát "lenyelni" (silent failure) vagy Nem Definiált Viselkedést (UB) okozni.

---

## 4. Invariáns-rács és Műveletek

A `VectorLane` hierarchikusan ellenőrzi a saját axiómáit:

```cpp
// Részlet az Invariáns Ellenőrzőből
constexpr ErrorLattice::Node check_invariants() const noexcept {
    ErrorLattice::Node current_error = ErrorLattice::NoError;
    bool vacuum_detected = false;

    for (size_t i = 0; i < N; ++i) {
        if (!mask_[i]) {
            vacuum_detected = true;
        } else if (vacuum_detected) {
            // Axiom 3 Violation: Rés a folytonos szubsztanciában
            current_error = ErrorLattice::Join(current_error, ErrorLattice::InvalidMask);
            current_error = ErrorLattice::Join(current_error, ErrorLattice::TopologyViolation);
        }
    }
    return current_error;
}
```

Amikor két `VectorLane`-t összeadunk (`lane1.add(lane2)`):
1. Ha a dimenzióik nem egyeznek (`N != M`), a rács automatikusan felveszi a `LaneCountMismatch` prímszámot, ami eszkalálódik `TopologyViolation`-né.
2. Az új szubsztancia-mező (mask) a két szülő-maszk **Algebrai Meet** (AND) művelete lesz.
3. A végén a létrejövő új sokaság automatikusan újraellenőrzi a topológiai integritását, és az összes felhalmozott hibát belezárja az új `Validated<T>` monádba.

---

## 5. Anti-Cheat és TDD Bizonyítékok

A rendszer tudatosan ignorálja a hardver-specifikus (pl. Intel AVX) utasításokat ezen a rétegen, hogy a fordító (GCC/Clang) auto-vektorizátora dolgozhasson az elméletileg hibátlan, tiszta C++20 kódon.

A megoldást a `slabflux_rte/tests/compute/axiomatic_vector_test.cpp` Google Test csomag validálja, amely bizonyítja:
- `construct_with_invalid_mask()` → Elkapja a TopologyViolation-t és pánikol.
- `add_with_mismatched_lane_count()` → Elkapja a LaneCountMismatch-et és pánikol.
- `partial_initialization()` → Elkapja a Dirty Zeroes (InvalidState) anomáliát.