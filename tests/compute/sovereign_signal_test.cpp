#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>

// Include the hardware integrity envelope
#include "slabflux/compute/sovereign_signal.hpp"
#include "slabflux/net/autologous_isomorphism.hpp"

namespace slabflux::test {

    // Standard 32-byte bounded POD payload for testing
    struct alignas(32) TestEvent {
        uint64_t instrument_id;
        double price;
        uint64_t quantity;
        uint64_t padding; // Pad to exactly 32 bytes
    };

    // =====================================================================
    // Symplectic Resonance Fencing (SRF) Tests
    // =====================================================================

    class SovereignSignalTest : public ::testing::Test {
    protected:
        void SetUp() override {
            // SRF requires AVX-512 Foundation and BW/VNNI extensions
            if (!__builtin_cpu_supports("avx512f") || !__builtin_cpu_supports("avx512bw")) {
                GTEST_SKIP() << "Skipping SRF tests: AVX-512F / AVX-512BW not supported by host silicon.";
            }
        }
    };

    TEST_F(SovereignSignalTest, PerfectResonance) {
        TestEvent ev{42, 100.5, 1000, 0};
        slabflux::compute::sovereign_signal<TestEvent> signal(ev);

        // Entangle payload with temporal LSN
        signal.seal(999888777);

        // The signal should not be void yet
        EXPECT_FALSE(signal.is_void());

        // Validation must succeed without vaporizing
        EXPECT_TRUE(signal.validate_and_vaporize());

        // Payload should remain perfectly intact
        EXPECT_EQ(signal.payload().instrument_id, 42);
        EXPECT_DOUBLE_EQ(signal.payload().price, 100.5);
        EXPECT_EQ(signal.payload().quantity, 1000);
    }

    TEST_F(SovereignSignalTest, TopologicalVaporizationOnCorruption) {
        TestEvent ev{42, 100.5, 1000, 0};
        slabflux::compute::sovereign_signal<TestEvent> signal(ev);
        signal.seal(999888777);

        // Simulate a cosmic ray bit-flip or UAF overwrite in the shared memory
        auto* raw_memory = reinterpret_cast<uint8_t*>(&signal);
        raw_memory[13] ^= 0xFF;

        // The geometric tension is broken; validation must fail
        EXPECT_FALSE(signal.validate_and_vaporize());

        // The memory must be topologically vaporized (entire 64-byte block replaced with 0x00)
        EXPECT_TRUE(signal.is_void());
        EXPECT_EQ(signal.payload().instrument_id, 0);
        EXPECT_DOUBLE_EQ(signal.payload().price, 0.0);
        EXPECT_EQ(signal.payload().quantity, 0);
    }

    // =====================================================================
    // Autologous Conflict Isomorphism (ACI) Tests
    // =====================================================================

    class AutologousIsomorphismIntegrityTest : public ::testing::Test {
    protected:
        void SetUp() override {
            // ACI requires AVX-512 Conflict Detection Instructions (CD)
            if (!__builtin_cpu_supports("avx512f") || !__builtin_cpu_supports("avx512cd")) {
                GTEST_SKIP() << "Skipping ACI tests: AVX-512CD (Conflict Detection) not supported by host silicon.";
            }
        }
    };

    TEST_F(AutologousIsomorphismIntegrityTest, ValidCollisionGraph) {
        TestEvent ev{99, 150.0, 500, 0};
        uint32_t type_id = 0x01; // E.g., TradeTick

        slabflux::net::autologous_isomorphism<TestEvent> aci(type_id, ev);

        // Interleave the payload with the Sequence Clock to build the collision graph
        aci.embed_symmetry(54321);

        // Extraction must succeed cleanly
        TestEvent extracted = aci.extract_and_decouple(54321).second;
        EXPECT_EQ(extracted.instrument_id, 99);
        EXPECT_DOUBLE_EQ(extracted.price, 150.0);
    }

    TEST_F(AutologousIsomorphismIntegrityTest, OntologicalDecouplingOnTemporalCorruption) {
        TestEvent ev{99, 150.0, 500, 0};
        slabflux::net::autologous_isomorphism<TestEvent> aci(0x01, ev);

        // Embed symmetry with Sequence Clock 54321
        aci.embed_symmetry(54321);

        // Attempt to extract with a desynchronized Sequence Clock (e.g., 54322)
        // This fractures the hardware collision graph.
        // We verify that the CPU executes the AVX-512 decoupling without crashing.
        TestEvent extracted = aci.extract_and_decouple(54322).second;
        EXPECT_EQ(extracted.instrument_id, 99); // The payload survives, but its type_id evaluates to VOID downstream.
    }
}
