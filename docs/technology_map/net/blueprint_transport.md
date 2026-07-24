# Blueprint: transport.hpp

## Architectural Overview
Defines the geometric memory layout and padding boundaries (e.g., `alignas(64)`) necessary to safely move network payloads across multi-core execution environments without triggering False Sharing.