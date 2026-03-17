#pragma once

#include "ISymbolicLink.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace core::link::symbolic
{
    class LocalAdsLink
      : public IClient
      , public ISymbolicLink
    {
      public:
        explicit LocalAdsLink(std::string instanceName = "default");
        ~LocalAdsLink() override = default;

        auto connect(std::chrono::milliseconds timeout = NO_TIMEOUT)
          -> coro::Task<result::Result<void>> override;
        auto disconnect(std::chrono::milliseconds timeout = NO_TIMEOUT)
          -> coro::Task<result::Result<void>> override;

        auto readInto(std::string_view path,
                      std::span<std::byte> dest,
                      std::chrono::milliseconds timeout = NO_TIMEOUT)
          -> coro::Task<result::Result<size_t>> override;
        auto writeFrom(std::string_view path,
                       std::span<const std::byte> src,
                       std::chrono::milliseconds timeout = NO_TIMEOUT)
          -> coro::Task<result::Result<void>> override;

        auto subscribeRaw(std::string_view path,
                          size_t size,
                          SubscriptionType type = SubscriptionType::OnChange,
                          std::chrono::milliseconds interval = NO_TIMEOUT)
          -> coro::Task<result::Result<std::shared_ptr<RawSubscription>>> override;
        auto unsubscribeRaw(std::shared_ptr<RawSubscription> subscription)
          -> coro::Task<result::Result<void>> override;
        auto unsubscribeRawSync(uint64_t id) -> void override;

        auto status() const -> Status override;

        template<typename T>
        auto readSync(std::string_view path) -> T
        {
            T value{};
            readBytesSync(path, std::as_writable_bytes(std::span{ &value, 1 }));
            return value;
        }

        auto readBytesSync(std::string_view path, std::span<std::byte> dest) -> size_t;

        template<typename T>
        auto writeSync(std::string_view path, const T& value) -> void
        {
            writeBytesSync(path, std::as_bytes(std::span{ &value, 1 }));
        }

        auto writeBytesSync(std::string_view path, std::span<const std::byte> src) -> void;
        auto instanceName() const -> const std::string& { return m_instanceName; }

      private:
        struct SubscriptionContext
        {
            std::string path;
            size_t size{ 0 };
            SubscriptionType type{ SubscriptionType::OnChange };
            std::shared_ptr<RawSubscription> stream;
            std::vector<std::byte> lastValue;
        };

        auto ensureSymbolLocked(std::string_view path, size_t size) -> std::vector<std::byte>&;
        auto publishLocked(std::string_view path, const std::vector<std::byte>& value, bool changedOnly)
          -> void;

        std::string m_instanceName;
        mutable std::mutex m_mutex;
        Status m_status{ Status::Connected };
        uint64_t m_nextSubscriptionId{ 1 };
        std::unordered_map<std::string, std::vector<std::byte>> m_symbols;
        std::unordered_map<uint64_t, SubscriptionContext> m_subscriptions;
    };
}