#include <iostream>
#include <type_traits>
#include <chrono>
#include <fstream>

#include "map/OrderBook.hpp"
#include "map/core/Event.hpp"
#include "map/replay/LogReader.hpp"
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

    OrderBook book;   // no risk wired in replay_test
    std::uint64_t events = 0;

    auto start = std::chrono::steady_clock::now();

    while (auto evOpt = reader.readNext()) {
        auto& ev = *evOpt;
        ++events;

        std::visit(
            [&](auto&& e) {
                using T = std::decay_t<decltype(e)>;

                if constexpr (std::is_same_v<T, NewOrderEvent>) {
                    // Apply NEW to the book
                    book.addOrder(e.side, e.price, e.qty, e.symbol);

                } else if constexpr (std::is_same_v<T, CancelOrderEvent>) {
                    // Apply CANCEL to the book
                    book.cancelOrder(e.id);

                } else if constexpr (std::is_same_v<T, TradeEvent>) {
                    // Trades don't directly mutate the book here;
                    // they are implied by the matching engine.
                }
            },
            ev
        );
    }

    auto end  = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(end - start).count();
    double eps  = (secs > 0.0 ? static_cast<double>(events) / secs : 0.0);

    // Print summary
    std::cout << "Replay finished.\n";
    std::cout << "  Events replayed: " << events << "\n";
    std::cout << "  Elapsed seconds: " << secs << "\n";
    std::cout << "  Events/sec:      " << eps << "\n";
    std::cout << "Replay checksum: " << book.checksum() << "\n";

    // Write simple CSV for comparisons / plotting
    std::ofstream pr("perf_replay.csv");
    if (pr.is_open()) {
        pr << "events,seconds,events_per_sec\n";
        pr << events << "," << secs << "," << eps << "\n";
    }

    return 0;
}
