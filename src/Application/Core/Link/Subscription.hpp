#pragma once

#include "Coroutines/Channel.hpp"

namespace core::link
{
    enum class SubscriptionType
    {
        OnChange,
        Cyclic
    };

    struct RawSubscription
    {
        const uint64_t id;
        coro::RawBinaryChannel stream;
        RawSubscription(uint64_t i)
          : id(i)
        {
        }
    };

    template<typename T>
    struct Subscription
    {
        Subscription() = default;
        Subscription(std::shared_ptr<RawSubscription> sub)
          : raw(std::move(sub))
        {
            if (raw) {
                stream = coro::BinaryChannel<T>(raw->stream);
            }
        }

        auto isValid() const noexcept -> bool { return raw != nullptr; }
        auto id() const noexcept -> uint64_t { return raw ? raw->id : 0; }

        coro::BinaryChannel<T> stream;
        std::shared_ptr<RawSubscription> raw;
    };
}