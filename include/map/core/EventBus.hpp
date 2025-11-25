#pragma once

#include <functional>
#include <unordered_map>
#include <vector>
#include <typeindex>
#include <shared_mutex>

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
            std::unique_lock lk(mutex_);
            handlers_[type].push_back(std::move(wrapper));
        }

        template <typename EventT>
        void publish(const EventT& event) {
            auto type = std::type_index(typeid(EventT));
            std::vector<AnyHandler> local;
            {
                std::shared_lock lk(mutex_);
                auto it = handlers_.find(type);
                if (it == handlers_.end()) return;
                local = it->second; // copy to allow handler mutation inside callbacks
            }

            for (auto& fn : local) {
                fn(event);
            }
        }

    private:
        using AnyHandler = std::function<void(const EventBase&)>;
        std::unordered_map<std::type_index, std::vector<AnyHandler>> handlers_;
        mutable std::shared_mutex mutex_;
    };

} // namespace map
