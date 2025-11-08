#pragma once
#include "map/Types.hpp"
#include "map/Side.hpp"

namespace map {

    struct Order {
        OrderId  id;
        Side     side;
        Price    price;
        Quantity remaining;
    };

} // namespace map
