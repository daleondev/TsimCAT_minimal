#pragma once

#include "Link/ILink.hpp"
#include "Link/Subscription.hpp"

#include "Common/Result.hpp"

#include "Coroutines/Task.hpp"

#include <span>
#include <string_view>

namespace core::link
{
    class ISymbolicLink : virtual public ILink
    {
      public:
        auto asSymbolic() -> ISymbolicLink* override { return this; }
        auto getMode() const -> Mode override { return Mode::Symbolic; }

        // clang-format off
        virtual auto readInto(std::string_view path,
                              std::span<std::byte> dest,
                              std::chrono::milliseconds timeout = NO_TIMEOUT) -> coro::Task<result::Result<size_t>> = 0;

        virtual auto writeFrom(std::string_view path,
                               std::span<const std::byte> src,
                               std::chrono::milliseconds timeout = NO_TIMEOUT) -> coro::Task<result::Result<void>> = 0;

        virtual auto subscribeRaw(std::string_view path,
                                  size_t size,
                                  SubscriptionType type = SubscriptionType::OnChange,
                                  std::chrono::milliseconds interval = NO_TIMEOUT) -> coro::Task<result::Result<std::shared_ptr<RawSubscription>>> = 0;

        virtual auto unsubscribeRaw(std::shared_ptr<RawSubscription> subscription) -> coro::Task<result::Result<void>> = 0;
        virtual auto unsubscribeRawSync(uint64_t id) -> void = 0;
        // clang-format on

        template<typename T>
        auto read(std::string_view path, std::chrono::milliseconds timeout = NO_TIMEOUT)
          -> coro::Task<result::Result<T>>
        {
            T value{};
            auto res{ co_await readInto(path, std::as_writable_bytes(std::span{ &value, 1 }), timeout) };
            if (!res) {
                co_return std::unexpected(res.error());
            }
            co_return value;
        }

        auto write(std::string_view path, const auto& value, std::chrono::milliseconds timeout = NO_TIMEOUT)
          -> coro::Task<result::Result<void>>
        {
            co_return co_await writeFrom(path, std::as_bytes(std::span{ &value, 1 }), timeout);
        }

        template<typename T>
        auto subscribe(std::string_view path,
                       SubscriptionType type = SubscriptionType::OnChange,
                       std::chrono::milliseconds interval = NO_TIMEOUT)
          -> coro::Task<result::Result<Subscription<T>>>
        {
            auto rawSub{ co_await subscribeRaw(path, sizeof(T), type, interval) };
            if (!rawSub) {
                co_return std::unexpected(rawSub.error());
            }

            co_return Subscription<T>{ rawSub.value() };
        }

        template<typename T>
        auto unsubscribe(Subscription<T>& sub) -> coro::Task<result::Result<void>>
        {
            if (!sub.raw) {
                co_return result::success();
            }
            co_return co_await unsubscribeRaw(sub.raw);
        }
    };
}