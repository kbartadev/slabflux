# Operating System Isolation & Bare-Metal Tuning

To achieve mathematically verifiable zero-jitter execution, the SLABFLUX must fundamentally subjugate the Linux operating system long before the C++ runtime environment is even launched. This is orchestrated through a draconian sequence of kernel parameters and hardware-level isolation directives.

## Kernel Boot Configuration (`grub_config`)
The baseline template for `/etc/default/grub` injects specific parameters into the bootloader to physically partition the CPU and starve the OS scheduler of resources on designated trading cores:
* **`isolcpus=...`**: Evicts the specified physical cores from the Linux Symmetric Multiprocessing (SMP) scheduler domain. The OS is explicitly forbidden from scheduling user-space processes or background daemons on these cores.
* **`nohz_full=...`**: Disables the kernel tick timer on the isolated cores, permanently silencing periodic scheduling interrupts that would otherwise cause microsecond-level latency spikes.
* **`rcu_nocbs=...`**: Offloads Read-Copy-Update (RCU) kernel callbacks away from the trading cores, forcing dedicated "housekeeping" cores to absorb all asynchronous kernel maintenance overhead.

## Memory and Page Management
* **`transparent_hugepage=never`**: Completely disables Transparent Huge Pages (THP) at the kernel level. The RTE relies exclusively on explicitly mapped `hugetlbfs` pages (1GB or 2MB) to entirely eradicate Translation Lookaside Buffer (TLB) misses and prevent the kernel daemon (`khugepaged`) from halting CPU pipelines during page defragmentation.
