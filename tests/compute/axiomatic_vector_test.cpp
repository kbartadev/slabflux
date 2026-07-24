/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * ============================================================================
 * SLABFLUX SOFTWARE ENGINE - ONTOLOGICAL COMPUTE SUBSYSTEM
 * Copyright (c) 2026 Kristóf Barta (https://github.com/kbartadev)
 * ============================================================================
 */

#include <gtest/gtest.h>
#include "slabflux/compute/axiomatic_vector.hpp"

using namespace slabflux::compute::axiomatic;

class AxiomaticVectorTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// ----------------------------------------------------------------------------
// 1. Helyes inicializálás és extrakció
// ----------------------------------------------------------------------------
TEST_F(AxiomaticVectorTest, ValidConstructionAndExtraction) {
    int vals[] = {10, 20, 30, 40};
    bool masks[] = {true, true, true, true};
    
    auto validated = VectorLane<int, 4>::construct(vals, masks, 4);

    EXPECT_TRUE(validated.is_pure());
    EXPECT_EQ(validated.state(), ErrorLattice::NoError);
    
    // Extrakció probléma nélkül lefut
    EXPECT_NO_THROW({
        auto lane = validated.extract_or_panic();
        EXPECT_EQ(lane.read_data(0), 10);
        EXPECT_EQ(lane.read_data(3), 40);
        EXPECT_TRUE(lane.read_mask(2));
    });
}

// ----------------------------------------------------------------------------
// 2. Topológiai folytonosság szakadása (Vacuum in the middle)
// ----------------------------------------------------------------------------
TEST_F(AxiomaticVectorTest, TopologyViolationVacuumDetected) {
    int vals[] = {1, 2, 3};
    bool masks[] = {true, false, true}; // <- Szakadás a téridő-szövetben
    
    auto validated = VectorLane<int, 3>::construct(vals, masks, 3);

    EXPECT_FALSE(validated.is_pure());
    EXPECT_TRUE(ErrorLattice::Has(validated.state(), ErrorLattice::TopologyViolation));
    EXPECT_TRUE(ErrorLattice::Has(validated.state(), ErrorLattice::InvalidMask));
    
    // Pánik kiváltása extrakciónál
    EXPECT_THROW(validated.extract_or_panic(), const char*);
}

// ----------------------------------------------------------------------------
// 3. Hiányos inicializálás (Partial Initialization)
// ----------------------------------------------------------------------------
TEST_F(AxiomaticVectorTest, InvalidStatePartialInitialization) {
    int vals[] = {1, 2};
    bool masks[] = {true, true};
    
    // 4 kapacitás, de csak 2 van inicializálva
    auto validated = VectorLane<int, 4>::construct(vals, masks, 2);

    EXPECT_FALSE(validated.is_pure());
    EXPECT_TRUE(ErrorLattice::Has(validated.state(), ErrorLattice::InvalidState));
    EXPECT_THROW(validated.extract_or_panic(), const char*);
}

// ----------------------------------------------------------------------------
// 4. Algebrai és dimenzionális inkompatibilitás
// ----------------------------------------------------------------------------
TEST_F(AxiomaticVectorTest, LaneCountMismatchOnAddition) {
    int v1[] = {10, 20}; bool m1[] = {true, true};
    int v2[] = {5, 5, 5}; bool m2[] = {true, true, true};
    
    auto lane1 = VectorLane<int, 2>::construct(v1, m1, 2).extract_or_panic();
    auto lane2 = VectorLane<int, 3>::construct(v2, m2, 3).extract_or_panic();

    // Algebrai fúzió eltérő dimenziójú sokaságok között
    auto result = lane1.add(lane2);

    EXPECT_FALSE(result.is_pure());
    // A dimenzióhiba a TopologyViolation-be eszkalálódik
    EXPECT_TRUE(ErrorLattice::Has(result.state(), ErrorLattice::LaneCountMismatch));
    EXPECT_TRUE(ErrorLattice::Has(result.state(), ErrorLattice::TopologyViolation));
    
    // Tilos a korrupt eredmény kinyerése
    EXPECT_THROW(result.extract_or_panic(), const char*);
}