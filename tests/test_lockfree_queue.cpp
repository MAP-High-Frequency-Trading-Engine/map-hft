#include <catch2/catch_test_macros.hpp>
#include "map/core/LockFreeQueue.hpp"

using namespace map;

TEST_CASE("LockFreeQueue SPSC basic push/pop", "[lfq]") {
    LockFreeQueue<int, 8> q;
    REQUIRE(q.empty());
    REQUIRE(q.push(1));
    REQUIRE(q.push(2));
    int v = 0;
    REQUIRE(q.pop(v));
    REQUIRE(v == 1);
    REQUIRE(q.pop(v));
    REQUIRE(v == 2);
    REQUIRE(q.empty());
}
