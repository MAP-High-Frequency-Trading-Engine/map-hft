#pragma once

#include <fstream>
#include <string>
#include "map/core/Event.hpp"

namespace map {

    class Logger {
    public:
        explicit Logger(const std::string& filename);
        ~Logger();

        void log(const NewOrderEvent& e);
        void log(const CancelOrderEvent& e);
        void log(const TradeEvent& e);

    private:
        std::ofstream out_;
    };

} // namespace map
