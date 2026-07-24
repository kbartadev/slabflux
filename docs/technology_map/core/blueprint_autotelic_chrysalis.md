# Blueprint: autotelic_chrysalis.hpp

## Architectural Overview
Provides atomic state commit/rollback capabilities on a per-packet basis natively in hardware memory, isolating execution blocks from leaving partial/corrupt states behind upon failure.