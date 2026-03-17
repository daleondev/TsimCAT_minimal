#pragma once

#include "ISymbolicLink.hpp"

#include <Windows.h>
#include <TcAdsDef.h>
#include <TcAdsAPI.h>

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace core::link::symbolic
{
    class TcAdsDllClient
      : public IClient
      , public ISymbolicLink
    {
      public:
        TcAdsDllClient(std::string_view remoteNetId,
                       std::string ipAddress,
                       uint16_t port = AMSPORT_R0_PLC_TC3,
                       std::string_view localNetId = "");
        ~TcAdsDllClient() override;

        // clang-format off
        auto connect(std::chrono::milliseconds timeout = NO_TIMEOUT) -> coro::Task<result::Result<void>> override;
        auto disconnect(std::chrono::milliseconds timeout = NO_TIMEOUT) -> coro::Task<result::Result<void>> override;

        auto readInto(std::string_view path,
                      std::span<std::byte> dest,
                      std::chrono::milliseconds timeout = NO_TIMEOUT) -> coro::Task<result::Result<size_t>> override;
        auto writeFrom(std::string_view path,
                       std::span<const std::byte> src,
                       std::chrono::milliseconds timeout = NO_TIMEOUT) -> coro::Task<result::Result<void>> override;

        auto subscribeRaw(std::string_view path,
                          size_t size,
                          SubscriptionType type = SubscriptionType::OnChange,
                          std::chrono::milliseconds interval = NO_TIMEOUT) -> coro::Task<result::Result<std::shared_ptr<RawSubscription>>> override;
        auto unsubscribeRaw(std::shared_ptr<RawSubscription> subscription) -> coro::Task<result::Result<void>> override;
        auto unsubscribeRawSync(uint64_t id) -> void override;
        // clang-format on

        auto status() const -> Status override;

      private:
        struct SubscriptionContext
        {
            uint32_t symbolHandle{ 0 };
            uint32_t notificationHandle{ 0 };
            std::shared_ptr<RawSubscription> stream;
        };

        static void __stdcall NotificationCallback(AmsAddr* pAddr,
                                                   AdsNotificationHeader* pNotification,
                                                   unsigned long hUser);
        void OnNotification(AdsNotificationHeader* pNotification);

        auto getTimeout() const -> std::chrono::milliseconds;
        auto setTimeout(std::chrono::milliseconds timeout) const -> long;
        auto createSymbolHandle(std::string_view path) -> result::Result<uint32_t>;
        void releaseSymbolHandle(uint32_t handle);

        AmsAddr m_remoteAddress{};
        std::string m_ipAddress;
        std::string m_requestedLocalNetId;
        long m_portHandle{ 0 };
        std::chrono::milliseconds m_defaultTimeout{};

        mutable std::mutex m_mutex;
        uint32_t m_driverId;
        std::unordered_map<uint32_t, SubscriptionContext> m_subscriptionContexts;
    };
}