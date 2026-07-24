#include <gtest/gtest.h>
#include "slabflux/compute/reproducible_random.hpp"
#include <set>

using namespace slabflux::compute;

TEST(ReproducibleRandomTest, Determinism) {
    deterministic_rng rng1(42);
    deterministic_rng rng2(42);

    for(int i=0; i<100; ++i) {
        EXPECT_EQ(rng1.next(), rng2.next());
    }
}

TEST(ReproducibleRandomTest, DifferentSeeds) {
    deterministic_rng rng1(42);
    deterministic_rng rng2(43);

    for(int i=0; i<100; ++i) { rng1.next(); rng2.next(); }

    EXPECT_NE(rng1.next(), rng2.next());
}

TEST(ReproducibleRandomTest, Distribution) {
    deterministic_rng rng(12345);
    std::set<uint32_t> values;
    for(int i=0; i<1000; ++i) {
        values.insert(rng.next());
    }
    // High probability that 1000 samples from a good RNG are unique
    EXPECT_GT(values.size(), 995u);
}
