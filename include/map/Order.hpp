#pragma once

#include <string>
#include "map/Types.hpp"
#include "map/Side.hpp"

namespace map {

    // An order resting in the book.
    struct Order {
        OrderId    id;
        std::string symbol;
        Side        side;
        Price       price;
        Quantity    qty; // This represents the *remaining* quantity in the book

        // Default constructor
        Order() = default;

        // Explicit constructor matching the usage in OrderBook.cpp (line 42)
        Order(OrderId id,
              const std::string& symbol,
              Side side,
              Price price,
              Quantity initialQty)
            // Initializing members
            : id(id), symbol(symbol), side(side), price(price), qty(initialQty)
        {}
    };

} // namespace map