# Blueprint: hardware_shaper.hpp

## Architectural Overview
Hardware-level network traffic shaper enforcing strict Inter-Packet Gap (IPG) tolerances. Utilizes CPU pause instructions (`TPAUSE`, `_mm_pause`) and `__rdtsc` bounds to prevent switch-level micro-burst packet loss.