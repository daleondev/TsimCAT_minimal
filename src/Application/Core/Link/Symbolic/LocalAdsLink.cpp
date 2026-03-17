#include "LocalAdsLink.hpp"

#include <algorithm>

namespace core::link::symbolic
{
    LocalAdsLink::LocalAdsLink(std::string instanceName)
      : m_instanceName(std::move(instanceName))
    {
    }

    auto LocalAdsLink::connect(std::chrono::milliseconds) -> coro::Task<result::Result<void>>
    {
        std::scoped_lock lock(m_mutex);
        m_status = Status::Connected;
        co_return result::success();
    }

    auto LocalAdsLink::disconnect(std::chrono::milliseconds) -> coro::Task<result::Result<void>>
    {
        std::unordered_map<uint64_t, SubscriptionContext> subscriptions;
        {
            std::scoped_lock lock(m_mutex);
            m_status = Status::Disconnected;
            subscriptions = std::move(m_subscriptions);
            m_subscriptions.clear();
        }

        for (auto& [id, context] : subscriptions) {
            if (context.stream) {
                context.stream->stream.close();
            }
        }

        co_return result::success();
    }

    auto LocalAdsLink::readInto(std::string_view path, std::span<std::byte> dest, std::chrono::milliseconds)
      -> coro::Task<result::Result<size_t>>
    {
        co_return readBytesSync(path, dest);
    }

    auto LocalAdsLink::writeFrom(std::string_view path,
                                 std::span<const std::byte> src,
                                 std::chrono::milliseconds) -> coro::Task<result::Result<void>>
    {
        writeBytesSync(path, src);
        co_return result::success();
    }

    auto LocalAdsLink::subscribeRaw(std::string_view path,
                                    size_t size,
                                    SubscriptionType type,
                                    std::chrono::milliseconds)
      -> coro::Task<result::Result<std::shared_ptr<RawSubscription>>>
    {
        auto subscription = std::make_shared<RawSubscription>(m_nextSubscriptionId++);
        std::vector<std::byte> currentValue;

        {
            std::scoped_lock lock(m_mutex);
            auto& symbol = ensureSymbolLocked(path, size);
            currentValue = symbol;
            m_subscriptions.emplace(subscription->id,
                                    SubscriptionContext{ .path = std::string(path),
                                                         .size = size,
                                                         .type = type,
                                                         .stream = subscription,
                                                         .lastValue = currentValue });
        }

        if (!currentValue.empty()) {
            subscription->stream.push(currentValue);
        }

        co_return subscription;
    }

    auto LocalAdsLink::unsubscribeRaw(std::shared_ptr<RawSubscription> subscription)
      -> coro::Task<result::Result<void>>
    {
        if (subscription) {
            unsubscribeRawSync(subscription->id);
        }
        co_return result::success();
    }

    auto LocalAdsLink::unsubscribeRawSync(uint64_t id) -> void
    {
        std::shared_ptr<RawSubscription> subscription;
        {
            std::scoped_lock lock(m_mutex);
            if (auto it = m_subscriptions.find(id); it != m_subscriptions.end()) {
                subscription = it->second.stream;
                m_subscriptions.erase(it);
            }
        }

        if (subscription) {
            subscription->stream.close();
        }
    }

    auto LocalAdsLink::status() const -> Status
    {
        std::scoped_lock lock(m_mutex);
        return m_status;
    }

    auto LocalAdsLink::readBytesSync(std::string_view path, std::span<std::byte> dest) -> size_t
    {
        std::scoped_lock lock(m_mutex);
        auto& symbol = ensureSymbolLocked(path, dest.size());
        const auto bytesToCopy = std::min(dest.size(), symbol.size());
        std::copy_n(symbol.begin(), bytesToCopy, dest.begin());
        if (bytesToCopy < dest.size()) {
            std::fill(dest.begin() + static_cast<std::ptrdiff_t>(bytesToCopy), dest.end(), std::byte{});
        }
        return dest.size();
    }

    auto LocalAdsLink::writeBytesSync(std::string_view path, std::span<const std::byte> src) -> void
    {
        std::scoped_lock lock(m_mutex);
        auto& symbol = ensureSymbolLocked(path, src.size());
        const bool changed =
          symbol.size() != src.size() || !std::equal(symbol.begin(), symbol.end(), src.begin(), src.end());

        symbol.assign(src.begin(), src.end());
        publishLocked(path, symbol, changed);
    }

    auto LocalAdsLink::ensureSymbolLocked(std::string_view path, size_t size) -> std::vector<std::byte>&
    {
        auto& symbol = m_symbols[std::string(path)];
        if (symbol.size() < size) {
            symbol.resize(size, std::byte{});
        }
        return symbol;
    }

    auto LocalAdsLink::publishLocked(std::string_view path,
                                     const std::vector<std::byte>& value,
                                     bool changedOnly) -> void
    {
        for (auto& [id, context] : m_subscriptions) {
            if (context.path != path || !context.stream) {
                continue;
            }

            if (context.size != value.size()) {
                context.lastValue = value;
                context.stream->stream.push(value);
                continue;
            }

            const bool changed = context.lastValue != value;
            if (!changedOnly || context.type == SubscriptionType::Cyclic || changed) {
                context.lastValue = value;
                context.stream->stream.push(value);
            }
        }
    }
}