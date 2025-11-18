#include "map/replay/LogReader.hpp"
#include "map/core/Event.hpp"
#include "map/Types.hpp"

#include <cstdint>
#include <iostream>

namespace map {

    LogReader::LogReader(const std::string& filename)
        : in_(filename, std::ios::binary)
    {
        // no header / magic yet, just raw events
    }

    void LogReader::rewind() {
        in_.clear();
        in_.seekg(0, std::ios::beg);
    }

    std::string LogReader::readString() {
        std::uint32_t len = 0;
        if (!in_.read(reinterpret_cast<char*>(&len), sizeof(len))) {
            return {};
        }

        std::string s(len, '\0');
        if (!in_.read(s.data(), len)) {
            s.clear();
        }
        return s;
    }

    std::int64_t LogReader::readInt64() {
        std::int64_t v = 0;
        in_.read(reinterpret_cast<char*>(&v), sizeof(v));
        return v;
    }

    std::uint64_t LogReader::readUInt64() {
        std::uint64_t v = 0;
        in_.read(reinterpret_cast<char*>(&v), sizeof(v));
        return v;
    }

    std::optional<LogReader::EventVariant> LogReader::readNext() {
        if (!in_) {
            return std::nullopt;
        }

        std::uint8_t type = 0;
        if (!in_.read(reinterpret_cast<char*>(&type), sizeof(type))) {
            // EOF
            return std::nullopt;
        }

        switch (type) {
            case 1: { // NewOrderEvent
                NewOrderEvent e{};

                e.symbol = readString();
                if (!in_) return std::nullopt;

                auto px  = readInt64();
                auto qty = readInt64();
                std::int32_t sideVal = 0;
                in_.read(reinterpret_cast<char*>(&sideVal), sizeof(sideVal));
                if (!in_) return std::nullopt;

                // Price/Quantity are strong typedefs over an int-like type.
                // We wrote them as int64_t, so cast back down.
                e.price = Price{static_cast<int>(px)};
                e.qty   = Quantity{static_cast<int>(qty)};
                e.side  = static_cast<Side>(sideVal);

                return EventVariant{e};
            }

            case 2: { // CancelOrderEvent
                CancelOrderEvent e{};

                e.symbol = readString();
                if (!in_) return std::nullopt;

                auto idRaw = readUInt64();
                e.id = OrderId{idRaw};

                return EventVariant{e};
            }

            case 3: { // TradeEvent
                TradeEvent e{};

                e.symbol = readString();
                if (!in_) return std::nullopt;

                auto takerIdRaw = readUInt64();
                auto makerIdRaw = readUInt64();
                auto px         = readInt64();
                auto qty        = readInt64();
                std::int32_t sideVal = 0;
                in_.read(reinterpret_cast<char*>(&sideVal), sizeof(sideVal));
                if (!in_) return std::nullopt;

                e.takerId   = OrderId{takerIdRaw};
                e.makerId   = OrderId{makerIdRaw};
                e.price     = Price{static_cast<int>(px)};
                e.qty       = Quantity{static_cast<int>(qty)};
                e.takerSide = static_cast<Side>(sideVal);

                return EventVariant{e};
            }

            default:
                // Unknown / corrupt; bail out
                std::cerr
                    << "LogReader: Unknown event type encountered: "
                    << static_cast<int>(type)
                    << ". File corrupt or unsupported type.\n";
                return std::nullopt;
        }
    }

} // namespace map
