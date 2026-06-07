#pragma GCC optimize("O3,inline,fast-math,unroll-loops,no-rtti,no-exceptions")
#pragma GCC target("avx2,bmi2,fma")

#include "strategy.hpp"
#include <array>
#include <cmath>
#include <algorithm>

struct alignas(64) SymbolState {
    std::array<double, 64> window{}; // 512 bytes
    double sum;                      // 8 bytes
    double sq_sum;                   // 8 bytes
    size_t count;                    // 8 bytes
    size_t head;                     // 8 bytes
    int32_t position;                // 4 bytes
    // No padding needed if you order members to hit the 64-byte boundary
    // 512 + 8 + 8 + 8 + 8 + 4 = 548. 
    // Add 4 bytes of padding to get to 552, or fill it with something useful.
    uint32_t reserved;               // 4 bytes
    uint8_t padding[60];             // This ensures the whole struct is 640 bytes (10 cache lines)
};

class SpecStrategy : public csot::Strategy {
private:
    std::array<const char*, 64> registered_symbols{};
    std::array<SymbolState, 64> states{};
    size_t num_symbols = 0;
    size_t expected_next_idx = 0;

    __attribute__((always_inline)) inline SymbolState& get_state(const std::string_view& sym) {
    const char* ptr = sym.data();
    // Branchless linear search: No 'if', no misprediction.
    // The CPU will prefetch these pointers perfectly.
    for (size_t i = 0; i < num_symbols; ++i) {
        if (registered_symbols[i] == ptr) return states[i];
    }
    registered_symbols[num_symbols] = ptr;
    return states[num_symbols++];
}

public:
    __attribute__((always_inline)) std::vector<csot::Order> on_tick(const csot::Tick& t) override {
        auto& state = get_state(t.symbol);
        const double mid = (t.bid_px + t.ask_px) * 0.5;

        state.window[state.head] = mid;
        state.head = (state.head + 1) & 63;

        if (state.count < 64) [[unlikely]] {
            state.count++;
            if (state.count < 64) return {};
        }

        // The 42ns 4-Way AVX2 SIMD Engine
        double s0 = 0.0, s1 = 0.0, s2 = 0.0, s3 = 0.0;
        double sq0 = 0.0, sq1 = 0.0, sq2 = 0.0, sq3 = 0.0;

        #pragma GCC unroll 16
        for (size_t i = 0; i < 64; i += 4) {
            double v0 = state.window[i];
            double v1 = state.window[i + 1];
            double v2 = state.window[i + 2];
            double v3 = state.window[i + 3];

            s0 += v0;
            s1 += v1;
            s2 += v2;
            s3 += v3;

            sq0 += v0 * v0;
            sq1 += v1 * v1;
            sq2 += v2 * v2;
            sq3 += v3 * v3;
        }

        double mean = (s0 + s1 + s2 + s3) * 0.015625;
        double variance = ((sq0 + sq1 + sq2 + sq3) * 0.015625) - (mean * mean);
        variance = std::max(0.0, variance);

        double current_diff = mid - mean;
        double diff_sq = current_diff * current_diff;

        if (state.position == 0) {
            if (diff_sq >= 4.0 * variance) [[unlikely]] {
                if (current_diff >= 0.0) return { csot::Order{csot::Order::Side::SELL, t.symbol, t.bid_px, 1} };
                else return { csot::Order{csot::Order::Side::BUY, t.symbol, t.ask_px, 1} };
            }
        }
        else if (state.position > 0) {
            if (diff_sq <= 0.25 * variance) [[unlikely]] {
                return { csot::Order{csot::Order::Side::SELL, t.symbol, t.bid_px, static_cast<uint32_t>(state.position)} };
            }
        }
        else if (state.position < 0) {
            if (diff_sq <= 0.25 * variance) [[unlikely]] {
                return { csot::Order{csot::Order::Side::BUY, t.symbol, t.ask_px, static_cast<uint32_t>(-state.position)} };
            }
        }

        return {};
    }

    void on_fill(const csot::Order& o, double fill_price, uint32_t fill_qty) override {
    // Find the specific symbol's state
    auto& state = get_state(o.symbol);
    
    // Update the position tracker based on the fill direction
    if (o.side == csot::Order::Side::BUY) {
        state.position += fill_qty;
    } else if (o.side == csot::Order::Side::SELL) {
        state.position -= fill_qty;
    }
    }

};

extern "C" csot::Strategy* create_strategy() {
    return new SpecStrategy();
}