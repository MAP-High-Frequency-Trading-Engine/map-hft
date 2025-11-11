//
// Created by Avi Maslow on 11/9/25.
//
#pragma once

#include <fstream>
#include <optional>
#include <string>
#include <variant>

#include "map/core/Event.hpp"

namespace map {

    // Matches the binary format written by Logger.cpp
    class LogReader {
    public:
        using EventVariant = std::variant<NewOrderEvent, CancelOrderEvent, TradeEvent>;

        explicit LogReader(const std::string& filename);

        bool good() const { return in_.good(); }

        // Read next event of any type. Returns std::nullopt at EOF / error.
        std::optional<EventVariant> readNext();

        void rewind();

    private:
        std::ifstream in_;

        std::string readString();
        std::int64_t readInt64();
        std::uint64_t readUInt64();
    };

} // namespace map
