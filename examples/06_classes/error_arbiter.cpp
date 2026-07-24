#include "slabflux/rte/error_arbiter.hpp"
#include <iostream>
#include <thread>
#include <vector>

using namespace slabflux::rte;

int main() {
    // 1. Initialize Arbiter with a bounded power-of-two capacity
    error_arbiter<16384> arbiter;

    // 2. Register a Fatal/Critical escalation callback
    arbiter.set_escalation_policy(error_severity::critical, [](const error_record& rec) {
        std::cerr << "[ESCALATION] Critical fault detected in domain "
        << static_cast<int>(rec.domain) << ". Code: "
        << rec.code << "\n";

        // E.g., trigger graceful shutdown or page on-call engineer
        if (rec.severity == error_severity::fatal) {
            std::cerr << "Initiating failover procedures...\n";
        }
    });

    // 3. Record errors concurrently from multiple threads
    std::vector<std::thread> workers;
    for (int i = 0; i < 4; ++i) {
        workers.emplace_back([&arbiter, i]() {
            arbiter.record_error(error_domain::network, 100 + i, error_severity::warning, i);
        });
    }
    for (auto& w : workers) { w.join(); }

    // 4. Trigger an escalation event
    arbiter.record_error(error_domain::compute, 503, error_severity::fatal, 999);

    // 5. Query recent errors for diagnostics via lock-free pop
    error_record err;
    std::cout << "\n--- Recent Error Dump ---\n";
    while (arbiter.try_pop(err)) {
        std::cout << "Code: " << err.code << " | LSN: " << err.lsn << "\n";
    }

    return 0;
}
