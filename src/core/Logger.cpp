#include "map/core/Logger.hpp"
#include <cstdint>

namespace map {

    Logger::Logger(const std::string& filename)
        : out_(filename, std::ios::binary)
    {
        // optional: write a simple header/magic here
    }

    Logger::~Logger() {
        if (out_.is_open())
            out_.close();
    }

    namespace {
        void writeString(std::ofstream& out, const std::string& s) {
            std::uint32_t len = static_cast<std::uint32_t>(s.size());
            out.write(reinterpret_cast<const char*>(&len), sizeof(len));
            out.write(s.data(), len);
        }

        void writeInt64(std::ofstream& out, std::int64_t v) {
            out.write(reinterpret_cast<const char*>(&v), sizeof(v));
        }

        void writeUInt64(std::ofstream& out, std::uint64_t v) {
            out.write(reinterpret_cast<const char*>(&v), sizeof(v));
        }
    }

    void Logger::log(const NewOrderEvent& e) {
        if (!out_) return;
        std::uint8_t type = 1; // NewOrder
        out_.write(reinterpret_cast<const char*>(&type), sizeof(type));

        writeString(out_, e.symbol);
        writeInt64(out_, e.price.raw());
        writeInt64(out_, e.qty.raw());

        auto sideVal = static_cast<std::int32_t>(e.side);
        out_.write(reinterpret_cast<const char*>(&sideVal), sizeof(sideVal));
    }

    void Logger::log(const CancelOrderEvent& e) {
        if (!out_) return;
        std::uint8_t type = 2; // Cancel
        out_.write(reinterpret_cast<const char*>(&type), sizeof(type));

        writeString(out_, e.symbol);
        writeUInt64(out_, e.id.raw());
    }

    void Logger::log(const TradeEvent& e) {
        if (!out_) return;
        std::uint8_t type = 3; // Trade
        out_.write(reinterpret_cast<const char*>(&type), sizeof(type));

        writeString(out_, e.symbol);
        writeUInt64(out_, e.takerId.raw());
        writeUInt64(out_, e.makerId.raw());
        writeInt64(out_, e.price.raw());
        writeInt64(out_, e.qty.raw());

        auto sideVal = static_cast<std::int32_t>(e.takerSide);
        out_.write(reinterpret_cast<const char*>(&sideVal), sizeof(sideVal));
    }

} // namespace map
