# Blueprint: ancestor_expansion.hpp

## Architectural Overview
Static DAG generator. Recursively expands type inheritances and relationships into a flattened, topologically sorted typelist to guarantee deterministic event precedence.