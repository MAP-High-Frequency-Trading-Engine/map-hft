#pragma once

#include <functional>
#include <unordered_map>
#include <vector>
#include <typeindex>

#include "map/core/Event.hpp"

namespace map {

    class EventBus {
    public:
        template <typename EventT>
        using Handler = std::function<void(const EventT&)>;

        template <typename EventT>
        void subscribe(Handler<EventT> handler) {
            auto type = std::type_index(typeid(EventT));
            auto wrapper = [h = std::move(handler)](const EventBase& e) {
                h(static_cast<const EventT&>(e));
            };
            handlers_[type].push_back(std::move(wrapper));
        }

        template <typename EventT>
        void publish(const EventT& event) {
            auto type = std::type_index(typeid(EventT));
            auto it   = handlers_.find(type);
            if (it == handlers_.end()) return;

            for (auto& fn : it->second) {
                fn(event);
            }
        }

    private:
        using AnyHandler = std::function<void(const EventBase&)>;
        std::unordered_map<std::type_index, std::vector<AnyHandler>> handlers_;
    };

} // namespace map
