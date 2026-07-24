# Boot Sequence & OS Scripts

The C++ runtime is completely impotent without the precise execution of the OS Hardening Suite. Executed sequentially by the `guard.service` systemd unit during the OS boot sequence, these shell executables forcibly clear the silicon environment before the RTE is permitted to launch.

## The Master Bootstrapper: `init.sh`
The primary command-and-control script that orchestrates the invocation of all downstream isolation utilities, guaranteeing correct execution order and validating kernel parameters.

## Detailed Subsystem Scripts
* **`shield_cores.sh`**: The CPU partitioning engine. It dynamically manipulates Linux `cpusets` and `cgroups` to physically quarantine the designated trading cores, violently migrating all non-essential OS tasks back to the default scheduler domain.
* **`rcu_isolation.sh`**: The interrupt migrator. It traverses `/proc/irq/` to re-route all physical hardware interrupts (e.g., storage controllers, non-trading NICs) and RCU kernel callbacks strictly to the housekeeping cores, shielding the hot path from interrupt storms.
* **`silicon_priority.sh`**: The frequency dictator. It interfaces directly with `cpufreq` and MSRs to disable CPU C-states and P-states, locking the CPU Uncore and Ring-bus frequencies to their absolute maximum. This permanently eradicates power-management-induced latency jitter.
* **`pcie_latency_shield.sh`**: The bus tuner. It forcefully disables PCIe ASPM (Active State Power Management) to prevent link-state power throttling, and rigorously tunes the PCIe Max Payload Size (MPS) and Max Read Request Size (MRRS) specifically for the trading NIC to maximize DMA throughput.
* **`tdx_seal.sh`**: The cryptographic enforcer. Signals the hardware to initialize Intel TDX, sealing the application's entire memory footprint within a hardware-encrypted memory domain prior to the C++ Ignition phase.
