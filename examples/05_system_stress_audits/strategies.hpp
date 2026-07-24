#pragma once
#include "slabflux/meta.hpp"
#include "contexts.hpp"
#include "events.hpp"

struct RiskManager {
    uint64_t total_updates_seen = 0;

    bool on(const OrderBookUpdate& ev, BaseMarketContext& ctx) {
        total_updates_seen++;
        if (ev.best_bid >= ev.best_ask) {
            ctx.is_tradable = false;
            return false;
        }
        return true;
    }

    bool on(const TradeTick& ev, BaseMarketContext& ctx) {
        total_updates_seen++;
        return ev.price > 0.0;
    }
};

struct TradingEngine : public RiskManager {
    double current_signal = 0.0;

    void on(const OrderBookUpdate& ev, BookContext& ctx) {
        ctx.mid_price = (ev.best_bid + ev.best_ask) / 2.0;
        current_signal = ctx.mid_price * 1.0001;
    }

    void on(const TradeTick& ev, TradeContext& ctx) {
        ctx.impact = ev.size * 0.01;
        if (ev.side == 1) current_signal += ctx.impact;
        else current_signal -= ctx.impact;
    }
};
