#pragma once

#include "Types.hpp"

struct Order {
    OrderId  id;
    Side     side;
    Price    price;
    Quantity remaining;
};
