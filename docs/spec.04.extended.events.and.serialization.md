# Extended Events, Memory Layout & Serialization

The deterministic hot path requires that the size and memory layout of objects used by complex business logic be known and guaranteed at compile time. Dynamic allocation and the use of virtual function tables (vtables) are strictly forbidden.

## `slabflux::extends<Bases...>` & `slabflux::extended_event<T, ID, Bases...>`
The backbone of zero‑copy, vtable‑free multiple inheritance.
* **Matrix Fusion (Compile-Time):** Using C++ variadic templates and the CRTP (Curiously Recurring Template Pattern), `extended_event` fuses the various interfaces (e.g., `market_data`, `order_intent`) into a single, tightly packed memory block. No `dynamic_cast`, no virtual pointer (vptr) overhead.
* **Hardware-Driven Alignment:** The compiler enforces physical isolation between the object and adjacent data via C++20 `std::hardware_constructive_interference_size`. This ensures that alignment boundaries are determined by the physical properties of the target silicon, eliminating **False Sharing** and ensuring the proprietary memory layout is distinct from fixed-padding patterns found in public implementations.

## `slabflux::runtime_domain<Events...>`
The global orchestrator of typed memory pools.
* **O(1) Domain Allocation:** Instead of managing one large memory region, it initializes a dedicated lock‑free LIFO pool for each event type provided as a template parameter during the boot phase.
* **HugePage Backing:** The entire domain is mapped onto 1GB `hugetlbfs` pages, ensuring that event allocation and reclamation never incur a TLB (Translation Lookaside Buffer) miss.

## `slabflux::trivial_serializer<Event>`
A bit‑exact, zero‑overhead serialization engine.
* **POD-Only Serialization:** Accepts only TriviallyCopyable objects. Serialization performs no data conversion or buffer copying; the in‑memory bit pattern of the object is written directly to the network card’s (NIC) ring buffer via DMA (Direct Memory Access). O(1) time complexity.
