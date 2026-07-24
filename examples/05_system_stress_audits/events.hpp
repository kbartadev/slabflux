#pragma once
#include <cstdint>
#include "slabflux/meta.hpp"

struct OrderBookUpdate {
    static constexpr uint16_t ID = 1001;
    double best_bid;
    double best_ask;
    uint32_t bid_size;
    uint32_t ask_size;
};

struct TradeTick {
    static constexpr uint16_t ID = 1002;
    double price;
    uint32_t size;
    uint8_t side;
};
