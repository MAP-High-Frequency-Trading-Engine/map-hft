//
// Created by Avi Maslow on 11/9/25.
//
#include "map/replay/LogReader.hpp"

#include <cstdint>

namespace map {

    LogReader::LogReader(const std::string& filename)
        : in_(filename, std::ios::binary)
    {}

    void LogReader::rewind() {
        in_.clear();
        in_.seekg(0, std::ios::beg);
    }

    std::string LogReader::readString() {
        std::uint32_t len = 0;
        in_.read(reinterpret_cast<char*>(&len), sizeof(len));
        std::string s(len, '\0');
        if (len > 0) {
            in_.read(s.data(), len);
        }
        return s;
    }

    std::int64_t LogReader::readInt64() {
        std::int64_t v{};
        in_.read(reinterpret_cast<char*>(&v), sizeof(v));
        return v;
    }

    std::uint64_t LogReader::readUInt64() {
        std::uint64_t v{};
        in_.read(reinterpret_cast<char*>(&v), sizeof(v));
        return v;
    }

    std::optional<LogReader::EventVariant> LogReader::readNext() {
        if (!in_.good()) {
            return std::nullopt;
        }

        std::uint8_t type = 0;
        in_.read(reinterpret_cast<char*>(&type), sizeof(type));
        if (!in_.good()) {
            return std::nullopt;
        }

        switch (type) {
            case 1: {
                // NewOrderEvent
                NewOrderEvent e;
                e.symbol    = readString();
                e.price     = Price{readInt64()};
                e.qty       = Quantity{readInt64()};
                {
                    std::int32_t sideVal{};
                    in_.read(reinterpret_cast<char*>(&sideVal), sizeof(sideVal));
                    e.side = static_cast<Side>(sideVal);
                }
                return EventVariant{e};
            }
            case 2: {
                // CancelOrderEvent
                CancelOrderEvent e;
                e.symbol = readString();
                e.id     = OrderId{readUInt64()};
                return EventVariant{e};
            }
            case 3: {
                // TradeEvent
                TradeEvent e;
                e.symbol   = readString();
                e.takerId  = OrderId{readUInt64()};
                e.makerId  = OrderId{readUInt64()};
                e.price    = Price{readInt64()};
                e.qty      = Quantity{readInt64()};
                {
                    std::int32_t sideVal{};
                    in_.read(reinterpret_cast<char*>(&sideVal), sizeof(sideVal));
                    e.takerSide = static_cast<Side>(sideVal);
                }
                return EventVariant{e};
            }
            default:
                // Unknown record type: abort reading
                in_.setstate(std::ios::failbit);
                return std::nullopt;
        }
    }

} // namespace map
