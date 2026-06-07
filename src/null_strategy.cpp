#include "strategy.hpp"
#include <vector>

// Subclass the provided strategy interface
class NullStrategy : public csot::Strategy {
public:
    // Do absolutely nothing and return an empty vector of orders
    std::vector<csot::Order> on_tick(const csot::Tick&) override {
        return {};
    }

};

// Export the frozen ABI C-linkage factory so the engine can load it
extern "C" csot::Strategy* create_strategy() {
    return new NullStrategy();
}