#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <deque>
#include "histogram.hpp"
#include "strategy.hpp"
// #include "null_strategy.cpp"
#include <x86intrin.h>

std::vector<csot::Tick> load_ticks(const std::string& path) {
    std::vector<csot::Tick> ticks;
    
    // This deque will hold our symbol strings permanently in memory.
    // std::string_view inside csot::Tick will safely point here.
    std::deque<std::string> symbol_pool; 

    // Step 1: Open the file using std::ifstream
    std::ifstream inputFile(path, std::ios::binary);

    if (!inputFile.is_open()) {
        std::cerr << "Error: Could not open the file!" << std::endl;
        return {};
    }
    
    // Step 2: Read and discard the first line (the header) using std::getline
    std::string line;
    std::getline(inputFile, line);
    
    // Step 3: Loop through the remaining lines using std::getline
    
    // Reset positions at the start of EVERY new line read
    while(std::getline(inputFile, line)) {
        size_t pos = 0;
        size_t next_comma = 0;
        csot::Tick tick{};

        // 1. timestamp_ns
        next_comma = line.find(',', pos);
        tick.timestamp_ns = std::stoull(line.substr(pos, next_comma - pos));
        pos = next_comma + 1;

        // 2. symbol (String Interning Logic)
        next_comma = line.find(',', pos);
        std::string sym_str = line.substr(pos, next_comma - pos);
        
        // Check if the symbol already exists in our persistent memory pool
        bool found = false;
        for (const auto& existing_sym : symbol_pool) {
            if (existing_sym == sym_str) {
                tick.symbol = std::string_view(existing_sym);
                found = true;
                break;
            }
        }
        // If it's a new symbol, add it to the deque so it stays in memory forever
        if (!found) {
            symbol_pool.push_back(sym_str);
            tick.symbol = std::string_view(symbol_pool.back());
        }
        pos = next_comma + 1;

        // 3. bid_px (Must be double)
        next_comma = line.find(',', pos);
        tick.bid_px = std::stod(line.substr(pos, next_comma - pos));
        pos = next_comma + 1;

        // 4. ask_px (Must be double)
        next_comma = line.find(',', pos);
        tick.ask_px = std::stod(line.substr(pos, next_comma - pos));
        pos = next_comma + 1;

        // 5. bid_qty
        next_comma = line.find(',', pos);
        tick.bid_qty = std::stoul(line.substr(pos, next_comma - pos));
        pos = next_comma + 1;

        // 6. ask_qty (Reads to the end of the line)
        tick.ask_qty = std::stoul(line.substr(pos));
        
        // Store the successfully parsed tick
        ticks.push_back(tick);
    }

    return ticks;
}

int main() {
    // 1. Capture the returned vector
    std::vector<csot::Tick> ticks = load_ticks("data/synthetic_large.csv");

    // 2. Validate the vector is not empty
    if (ticks.empty()) {
        std::cerr << "Verification Failed: Vector is empty.\n";
        return 1;
    }

    // 3. Print the first tick
    // const auto& first = ticks.front();
    // std::cout << "--- FIRST TICK ---\n"
    //           << "Timestamp: " << first.timestamp_ns << "\n"
    //           << "Symbol:    " << first.symbol << "\n"
    //           << "Bid Px:    " << first.bid_px << "\n"
    //           << "Ask Px:    " << first.ask_px << "\n"
    //           << "Bid Qty:   " << first.bid_qty << "\n"
    //           << "Ask Qty:   " << first.ask_qty << "\n\n";

    // 4. Print the last tick
    // const auto& last = ticks.back();
    // std::cout << "--- LAST TICK ---\n"
    //           << "Timestamp: " << last.timestamp_ns << "\n"
    //           << "Symbol:    " << last.symbol << "\n"
    //           << "Bid Px:    " << last.bid_px << "\n"
    //           << "Ask Px:    " << last.ask_px << "\n"
    //           << "Bid Qty:   " << last.bid_qty << "\n"
    //           << "Ask Qty:   " << last.ask_qty << "\n";

    csot::Strategy* strategy = create_strategy();
    csot::LatencyHistogram hist;

    // Inside your engine's main loop:

        // 3. The Hot Loop
    for (const auto& tick : ticks) {
        // Serialize the pipeline to prevent instruction reordering
        _mm_lfence();
        uint64_t start = __rdtsc();
        _mm_lfence();
        
        // Execute the strategy logic
        std::vector<csot::Order> orders = strategy->on_tick(tick);
        
        // Serialize again to ensure execution is completely finished
        _mm_lfence();
        uint64_t end = __rdtsc();
        _mm_lfence();
        
        // Calculate raw CPU cycles
        uint64_t cycles = end - start;
        hist.record(cycles);

        // --- THE FILL MODEL ---
        for (const auto& order : orders) {
            strategy->on_fill(order, order.price, order.qty);
        }
    }

    // std::cout << "\n--- NULL STRATEGY LATENCY BASELINE ---\n";
    hist.print(std::cout);

    return 0;
}