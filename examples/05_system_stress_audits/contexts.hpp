
#pragma once
#include "slabflux/meta.hpp"
#include "events.hpp"
#include "slabflux/pipeline/context_vault.hpp"

struct BaseMarketContext {
    bool is_tradable = true;
};

struct BookContext : public BaseMarketContext {
    using slabflux_exclusive_event = OrderBookUpdate;
    double mid_price = 0.0;
};

struct TradeContext : public BaseMarketContext {
    using slabflux_exclusive_event = TradeTick;
    double impact = 0.0;
};

// STRICT COMPILE TIME MAPPING
REGISTER_CONTEXT(OrderBookUpdate, BookContext);
REGISTER_CONTEXT(TradeTick, TradeContext);
