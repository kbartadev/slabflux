# Blueprint: handler_base.hpp

## Architectural Overview
Lightweight structural base class for strategy handlers. Injects necessary compile-time traits (like `parents` typelists) via CRTP to integrate user logic seamlessly into the deterministic DAG without virtual overhead.