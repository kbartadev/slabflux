# Blueprint: dispatch_unroller.hpp

## Architectural Overview
Zero-cost compile-time loop unroller. Expands event-to-handler routing into branchless inline execution paths using fold expressions, completely eliminating virtual dispatch.