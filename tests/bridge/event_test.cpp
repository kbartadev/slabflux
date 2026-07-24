#include <gtest/gtest.h>
#include <atomic>
#include "slabflux/core.hpp"

using namespace slabflux;

struct simple_event {
    std::atomic<int>* counter;
    simple_event(std::atomic<int>* c) : counter(c) {
        if (counter) counter->fetch_add(1, std::memory_order_relaxed);
    }
    ~simple_event() {
        if (counter) counter->fetch_sub(1, std::memory_order_relaxed);
    }
};

TEST(EventPtrTest, managed_data_automatically_reclaims_memory_on_scope_exit) {
    std::atomic<int> alive{0};
    pool<simple_event, 5> p;
    
    {
        auto ptr = p.make(&alive);
        EXPECT_EQ(alive.load(), 1);
    } 

    EXPECT_EQ(alive.load(), 0);
}

TEST(EventPtrTest, managed_data_explicit_release_and_manual_cleanup) {
    std::atomic<int> alive{0};
    pool<simple_event, 5> p;
    
    auto ptr = p.make(&alive);
    EXPECT_EQ(alive.load(), 1);
    
    simple_event* raw = ptr.release(); 
    EXPECT_EQ(alive.load(), 1);
    
    p.release(raw);
    EXPECT_EQ(alive.load(), 0);
}

TEST(EventPtrTest, managed_data_move_semantics_preserve_unique_ownership) {
    std::atomic<int> alive{0};
    pool<simple_event, 5> p;
    
    auto ptr1 = p.make(&alive);
    auto ptr2 = std::move(ptr1);

    EXPECT_FALSE(ptr1);
    EXPECT_TRUE(ptr2);
    EXPECT_EQ(alive.load(), 1); // No duplicate increments/decrements
}
