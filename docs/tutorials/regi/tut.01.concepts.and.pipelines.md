# Tutorial 1: The Zero-Overhead Philosophy & C++20 Concepts

Welcome to SLABFLUX. If you are coming from traditional Object-Oriented Programming (OOP) paradigms with virtual inheritance, dynamic dispatch, and `std::shared_ptr`, you are entering a radically different domain. 

Whether you are building a high-tickrate multiplayer game backend, an ultra-low latency trading engine, or a real-time network gateway, the convenience of runtime polymorphism is a liability. The architecture strips away legacy CRTP base classes (`handler_base`) and RAII wrappers (`event_ptr`), replacing them with **Structural Recognition** using C++20 Concepts.

## 1. Structural Recognition (Duck-Typing)

In SLABFLUX, a "Handler" no longer inherits from a framework-specific base class. It is a plain, isolated C++ `struct`. The framework recognizes it purely by its signature at compile time. This means zero virtual tables (vtable) and zero cache misses.

```cpp
#include "slabflux/core.hpp"
#include <iostream>

// 1. Independent Events (No common base needed for the logic layer)
struct player_movement { 
    double velocity; 
    player_movement(double v) : velocity(v) {} 
};

struct market_trade { 
    double velocity; // Represents price velocity
    market_trade(double v) : velocity(v) {} 
};

// 2. The C++20 Concept Definition
template <typename T> 
concept HasVelocity = requires(T a) { a.velocity; };

// 3. The Isolated High-Performance Handler
struct physics_analytics {
    // Matches ANY event that has a 'velocity' field. No inheritance required!
    template <HasVelocity E> 
    inline void on(const E* ev) { 
        if (!ev) return;
        std::cout << "[Analytics] Processed velocity vector: " << ev->velocity << "\n"; 
    }
};
```

## 2. Compile-Time Inlining Constraints

By defining static invariants using standard C++20 Concepts, we can construct generic processing components that accept any incoming event type matching the expected data shape, without paying a single runtime cycle for abstraction.

```cpp
// A structural concept that validates the presence of a 'velocity' property
template <typename T> 
concept HasVelocity = requires(T a) { a.velocity; };

// The Isolated High-Performance Handler
struct physics_analytics {
    // Matches ANY event that implements a 'velocity' field at compile-time.
    template <HasVelocity E> 
    inline void on(const E* ev) noexcept { 
        if (!ev) [[unlikely]] return;
        std::cout << "[Analytics] Current velocity state: " << ev->velocity << "\n"; 
    }
};
```
