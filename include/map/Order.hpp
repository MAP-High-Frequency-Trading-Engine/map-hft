#pragma once

#include <string>
#include "map/Types.hpp"
#include "map/Side.hpp"

namespace map {

    // An order resting in the book.
    struct Order {
        OrderId     id;
        std::string symbol;
        Side        side;
        Price       px;   // price in ticks
        Quantity    qty;  // remaining quantity in the book

        Order() = default;

        Order(OrderId id_,
              const std::string& symbol_,
              Side side_,
              Price px_,
              Quantity initialQty)
            : id(id_),
              symbol(symbol_),
              side(side_),
              px(px_),          // <-- match the member name
              qty(initialQty)
        {}
    };

} // namespace map
