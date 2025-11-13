//
// Simple replay tool: reads events.bin and rebuilds the book,
// then prints a checksum of the final state.
//
// Uses the existing LogReader::readNext() API.
//

#include <iostream>
#include <string>
#include <type_traits>

#include "map/OrderBook.hpp"
#include "map/core/Event.hpp"
#include "map/replay/LogReader.hpp"
#include "map/risk/RiskLimits.hpp"
#include "map/Types.hpp"

using namespace map;

int main(int argc, char** argv) {
    std::string filename = "events.bin";
    if (argc > 1) {
        filename = argv[1];
    }

    LogReader reader(filename);
    if (!reader.good()) {
        std::cerr << "Failed to open log file: " << filename << "\n";
        return 1;
    }

    // Very loose limits so replay won't reject anything
    RiskConfig cfg{
        .maxOrderSize = Quantity{1'000'000},
        .maxPosition  = Quantity{10'000'000},
        .maxNotional  = Notional{1'000'000'000}
    };
    RiskLimits risk(cfg);

    OrderBook book(&risk);

    // LogReader::readNext() is assumed to return std::optional<variant<...>>
    while (auto evOpt = reader.readNext()) {
        std::visit([&](auto&& e) {
            using T = std::decay_t<decltype(e)>;

            if constexpr (std::is_same_v<T, NewOrderEvent>) {
                // If NewOrderEvent has a .symbol field, use it:
                book.addOrder(e.side, e.price, e.qty, e.symbol);
                // If not, temporarily:
                // book.addOrder(e.side, e.price, e.qty, "TEST");
            }
            else if constexpr (std::is_same_v<T, CancelOrderEvent>) {
                book.cancelOrder(e.id);
            }
            else if constexpr (std::is_same_v<T, TradeEvent>) {
                // No book mutation needed; trades are implied by matching
            }
        }, *evOpt);
    }

    std::cout << "Replay checksum: " << book.checksum() << "\n";
    return 0;
}
