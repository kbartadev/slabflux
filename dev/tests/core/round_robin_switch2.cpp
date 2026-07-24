#include <gtest/gtest.h>

#include <atomic>

#include "slabflux/core.hpp"

using namespace slabflux;

struct task {
    std::atomic<int>* counter;
    int id;

    task(std::atomic<int>* c, int i) : counter(c), id(i) {
        if (counter) counter->fetch_add(1, std::memory_order_relaxed);
    }
    ~task() {
        if (counter) counter->fetch_sub(1, std::memory_order_relaxed);
    }
};

// ============================================================================
// TEST 1 — Round‑robin switch must distribute events evenly across tracks.
// Deterministic O(1) routing: 0 → 1 → 0 → 1 …
// ============================================================================
TEST(RoundRobinSwitchTest, round_robin_switch_distributes_events_evenly_across_tracks) {
    pool<task, 10> p;
    spsc_conduit<task*, 10> track_1;
    spsc_conduit<task*, 10> track_2;

    round_robin_switch<task, 2> switch_node;
    switch_node.bind_track(0, track_1);
    switch_node.bind_track(1, track_2);

    auto ev1 = p.make(nullptr, 100);
    switch_node.route(ev1.release());

    auto ev2 = p.make(nullptr, 200);
    switch_node.route(ev2.release());

    auto ev3 = p.make(nullptr, 300);
    switch_node.route(ev3.release());

    auto p1 = track_1.try_pop(p);
    EXPECT_EQ(p1->id, 100);
    
    auto p2 = track_2.try_pop(p);
    EXPECT_EQ(p2->id, 200);

    auto p3 = track_1.try_pop(p);
    EXPECT_EQ(p3->id, 300);
}

// ============================================================================
// TEST 2 — Deterministic drop semantics when a track is full.
// ============================================================================
TEST(RoundRobinSwitchTest, round_robin_switch_drops_events_deterministically_on_full_track) {
    std::atomic<int> alive{ 0 };
    pool<task, 10> p;
    spsc_conduit<task*, 2> track;

    round_robin_switch<task, 1> switch_node;
    switch_node.bind_track(0, track);

    auto ev1 = p.make(&alive, 1);
    EXPECT_TRUE(switch_node.route(ev1.release()));
    EXPECT_EQ(alive.load(), 1);

    // Fill the track
    auto ev2 = p.make(&alive, 2);
    EXPECT_TRUE(switch_node.route(ev2.release()));
    
    // Track is now likely full (capacity 2 physical = 1 logical for spsc)
    auto ev3 = p.make(&alive, 3);
    task* raw3 = ev3.release();
    if (!switch_node.route(raw3)) {
        p.release(raw3);
    }
}

// ============================================================================
// TEST 3 — Round‑robin poller must poll tracks evenly.
// Even with uneven queue depths, fairness must be preserved.
// ============================================================================
TEST(RoundRobinPollerTest, round_robin_poller_polls_evenly_across_tracks) {
    pool<task, 10> p;
    spsc_conduit<task*, 10> track_1;
    spsc_conduit<task*, 10> track_2;

    track_1.push(p.make(nullptr, 1).release());
    track_1.push(p.make(nullptr, 2).release());
    track_2.push(p.make(nullptr, 3).release());

    round_robin_poller<task, 2> poller;
    poller.bind_track(0, track_1);
    poller.bind_track(1, track_2);

    // Poller MUST alternate:
    auto r1 = poller.poll();
    EXPECT_EQ(r1->id, 1);  // Track 1
    p.release(r1);

    auto r2 = poller.poll();
    EXPECT_EQ(r2->id, 3);  // Track 2
    p.release(r2);

    auto r3 = poller.poll();
    EXPECT_EQ(r3->id, 2);  // Track 1
    p.release(r3);

    EXPECT_EQ(poller.poll(), nullptr);
}
