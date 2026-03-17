#pragma once

#include "ISymbolicLink.hpp"

#include <AdsLib/AdsDevice.h>
#include <AdsLib/AdsLib.h>
#include <AdsLib/AdsNotificationOOI.h>
#include <AdsLib/AdsVariable.h>

#include <string>
#include <unordered_map>

namespace core::link::symbolic
{
    class AdsClient
      : public IClient
      , public ISymbolicLink
    {
      public:
        AdsClient(std::string_view remoteNetId,
                  std::string ipAddress,
                  uint16_t port = AMSPORT_R0_PLC_TC3,
                  std::string_view localNetId = "");
        ~AdsClient() override;

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
        auto getTimeout() -> std::chrono::milliseconds;
        auto setTimeout(std::chrono::milliseconds timeout) -> void;

        struct SubscriptionContext
        {
            AdsHandle symbolHandle;
            AdsHandle notificationHandle;
            std::shared_ptr<RawSubscription> stream;
        };

        static void NotificationCallback(const AmsAddr* pAddr,
                                         const AdsNotificationHeader* pNotification,
                                         uint32_t hUser);
        void OnNotification(const AdsNotificationHeader* pNotification);

        AmsNetId m_remoteNetId;
        std::string m_ipAddress;
        uint16_t m_port;

        std::unique_ptr<AdsDevice> m_route;
        std::chrono::milliseconds m_defaultTimeout;

        std::mutex m_mutex;
        uint32_t m_driverId;
        std::unordered_map<uint32_t, SubscriptionContext> m_subscriptionContexts;
    };

}