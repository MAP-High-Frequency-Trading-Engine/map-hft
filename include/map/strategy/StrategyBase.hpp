#pragma once

#include <cstdint>
#include "map/core/EventBus.hpp"

namespace map::strategy {

class StrategyBase {
public:
    explicit StrategyBase(EventBus& bus) : bus_(bus) {}
    virtual ~StrategyBase() = default;

    virtual void onTick(std::uint64_t nowMs) = 0;

protected:
    EventBus& bus_;
};

} // namespace map::strategy
