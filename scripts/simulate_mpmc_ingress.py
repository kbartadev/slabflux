#!/usr/bin/env python3
import os
import subprocess
import tempfile
from pathlib import Path

CPP_SOURCE = """
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <iomanip>

struct alignas(64) mpmc_slot {
    uint64_t payload;
    uint32_t length;
    uint32_t padding[10];
    uint32_t phase_tag;
};

struct mpmc_conduit {
    mpmc_slot* ring;
    uint32_t ring_size;
    alignas(64) uint32_t claim_index{0}; 

    void push(uint64_t data) {
        uint32_t ticket = __atomic_fetch_add(&claim_index, 1, __ATOMIC_RELAXED);
        uint32_t actual_idx = ticket & (ring_size - 1);
        uint32_t lap_count = ticket / ring_size;
        uint32_t calculated_phase = (lap_count % 255) + 1;

        ring[actual_idx].payload = data;
        ring[actual_idx].length = 8;

        asm volatile("sfence" ::: "memory");
        __atomic_store_n(&ring[actual_idx].phase_tag, calculated_phase, __ATOMIC_RELEASE);
    }
};

int main() {
    constexpr uint32_t RING_SIZE = 65536; // Power of 2
    constexpr uint64_t TOTAL_MESSAGES = 50'000'000;
    constexpr int NUM_PRODUCERS = 4;
    
    std::vector<mpmc_slot> buffer(RING_SIZE);
    mpmc_conduit conduit{ buffer.data(), RING_SIZE };

    std::atomic<bool> start_flag{false};

    auto producer_task = & {
        uint64_t messages_per_thread = TOTAL_MESSAGES / NUM_PRODUCERS;
        while (!start_flag.load(std::memory_order_acquire)) {
            asm volatile("pause" ::: "memory");
        }
        for (uint64_t i = 0; i < messages_per_thread; ++i) {
            conduit.push(id + i);
        }
    };

    std::vector<std::thread> producers;
    for (int i = 0; i < NUM_PRODUCERS; ++i) {
        producers.emplace_back(producer_task, i);
    }

    uint32_t current_idx = 0;
    uint32_t expected_phase = 1;
    uint64_t received = 0;

    auto start_time = std::chrono::high_resolution_clock::now();
    start_flag.store(true, std::memory_order_release);

    // Single Consumer Logic (The Sovereign Core)
    while (received < TOTAL_MESSAGES) {
        uint32_t tag = __atomic_load_n(&conduit.ring[current_idx].phase_tag, __ATOMIC_ACQUIRE);
        
        if (tag == expected_phase) {
            received++;
            current_idx = (current_idx + 1) & (RING_SIZE - 1);
            if (current_idx == 0) [[unlikely]] {
                expected_phase = (expected_phase + 1) & 0xFF;
                if (expected_phase == 0) expected_phase = 1;
            }
        } else {
            asm volatile("pause" ::: "memory");
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;
    
    for (auto& t : producers) t.join();

    std::cout << "[INFO] Validated sequential extraction of " << received << " messages.\\n";
    std::cout << "[INFO] MPMC Throughput: " << std::fixed << std::setprecision(0) 
              << (TOTAL_MESSAGES / elapsed.count()) << " ops/sec\\n";
    return 0;
}
"""

def main():
    with tempfile.NamedTemporaryFile(suffix=".cpp", delete=False) as tmp_cpp:
        tmp_cpp.write(CPP_SOURCE.encode('utf-8'))
        cpp_path = tmp_cpp.name

    exe_path = cpp_path[:-4] + ".out"
    
    print("Compiling MPMC Benchmark with g++ -O3 -std=c++20...")
    subprocess.run(["g++", "-O3", "-std=c++20", "-pthread", cpp_path, "-o", exe_path], check=True)
    print("Executing Benchmark...\n")
    subprocess.run([exe_path], check=True)

if __name__ == "__main__":
    main()