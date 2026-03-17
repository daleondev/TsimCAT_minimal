#include "TcAdsDllClient.hpp"

#include <atomic>
#include <cassert>
#include <charconv>
#include <iostream>
#include <mutex>
#include <print>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace
{
    static auto strToNetId(std::string_view str) -> AmsNetId
    {
        auto parts{ str | std::views::split('.') };
        AmsNetId netId{};

        auto i{ 0uz };
        for (const auto& part : parts) {
            assert(i < 6 && "Invalid NetId string");
            std::from_chars(part.data(), part.data() + part.size(), netId.b[i]);
            ++i;
        }
        return netId;
    }

    class TcAdsDllErrorCategory : public std::error_category
    {
      public:
        const char* name() const noexcept override { return "TcAdsDll"; }

        std::string message(int ev) const override { return std::to_string(ev); }
    };

    auto ads_category() -> const std::error_category&
    {
        static TcAdsDllErrorCategory instance;
        return instance;
    }

    auto make_error_code(long code) -> std::error_code
    {
        return std::error_code(static_cast<int>(code), ads_category());
    }

    static std::mutex s_registryMutex;
    static std::unordered_map<uint32_t, core::link::symbolic::TcAdsDllClient*> s_registry;
    static std::atomic<uint32_t> s_nextDriverId{ 1 };
}

namespace core::link::symbolic
{
    TcAdsDllClient::TcAdsDllClient(std::string_view remoteNetId,
                                   std::string ipAddress,
                                   uint16_t port,
                                   std::string_view localNetId)
      : m_remoteAddress{ .netId = strToNetId(remoteNetId), .port = port }
      , m_ipAddress(std::move(ipAddress))
      , m_requestedLocalNetId(localNetId)
      , m_driverId(s_nextDriverId++)
    {
        std::scoped_lock lock(s_registryMutex);
        s_registry[m_driverId] = this;
    }

    TcAdsDllClient::~TcAdsDllClient()
    {
        (void)disconnect(std::chrono::milliseconds(0));

        std::scoped_lock lock(s_registryMutex);
        s_registry.erase(m_driverId);
    }

    auto TcAdsDllClient::connect(std::chrono::milliseconds timeout) -> coro::Task<result::Result<void>>
    {
        if (m_portHandle != 0) {
            co_return result::success();
        }

        m_portHandle = AdsPortOpenEx();
        if (m_portHandle == 0) {
            co_return std::unexpected(make_error_code(ADSERR_CLIENT_PORTNOTOPEN));
        }

        m_defaultTimeout = getTimeout();
        auto timeoutStatus = setTimeout(timeout);
        if (timeoutStatus != ADSERR_NOERR) {
            (void)AdsPortCloseEx(m_portHandle);
            m_portHandle = 0;
            co_return std::unexpected(make_error_code(timeoutStatus));
        }

        AmsAddr localAddress{};
        auto localAddressStatus = AdsGetLocalAddressEx(m_portHandle, &localAddress);
        if (localAddressStatus != ADSERR_NOERR) {
            (void)AdsPortCloseEx(m_portHandle);
            m_portHandle = 0;
            co_return std::unexpected(make_error_code(localAddressStatus));
        }

        AdsVersion version{};
        char deviceName[ADS_FIXEDNAMESIZE + 1]{};
        auto deviceInfoStatus =
          AdsSyncReadDeviceInfoReqEx(m_portHandle, &m_remoteAddress, deviceName, &version);
        if (deviceInfoStatus != ADSERR_NOERR) {
            (void)AdsPortCloseEx(m_portHandle);
            m_portHandle = 0;
            co_return std::unexpected(make_error_code(deviceInfoStatus));
        }

        co_return result::success();
    }

    auto TcAdsDllClient::disconnect(std::chrono::milliseconds timeout) -> coro::Task<result::Result<void>>
    {
        if (m_portHandle == 0) {
            co_return result::success();
        }

        (void)setTimeout(timeout);

        std::vector<uint32_t> ids;
        {
            std::scoped_lock lock(m_mutex);
            ids.reserve(m_subscriptionContexts.size());
            for (const auto& [id, context] : m_subscriptionContexts) {
                ids.push_back(id);
            }
        }

        for (const auto id : ids) {
            unsubscribeRawSync(id);
        }

        auto closeStatus = AdsPortCloseEx(m_portHandle);
        m_portHandle = 0;

        if (closeStatus != ADSERR_NOERR) {
            co_return std::unexpected(make_error_code(closeStatus));
        }

        co_return result::success();
    }

    auto TcAdsDllClient::status() const -> Status
    {
        return m_portHandle != 0 ? Status::Connected : Status::Disconnected;
    }

    auto TcAdsDllClient::readInto(std::string_view path,
                                  std::span<std::byte> dest,
                                  std::chrono::milliseconds timeout) -> coro::Task<result::Result<size_t>>
    {
        if (m_portHandle == 0) {
            co_return std::unexpected(make_error_code(ADSERR_CLIENT_PORTNOTOPEN));
        }

        auto timeoutStatus = setTimeout(timeout);
        if (timeoutStatus != ADSERR_NOERR) {
            co_return std::unexpected(make_error_code(timeoutStatus));
        }

        auto handleResult = createSymbolHandle(path);
        if (!handleResult) {
            co_return std::unexpected(handleResult.error());
        }

        const auto handle = handleResult.value();
        unsigned long bytesRead = 0;
        auto statusCode = AdsSyncReadReqEx2(m_portHandle,
                                            &m_remoteAddress,
                                            ADSIGRP_SYM_VALBYHND,
                                            handle,
                                            static_cast<unsigned long>(dest.size()),
                                            dest.data(),
                                            &bytesRead);
        releaseSymbolHandle(handle);

        if (statusCode != ADSERR_NOERR) {
            co_return std::unexpected(make_error_code(statusCode));
        }

        if (bytesRead != dest.size()) {
            co_return std::unexpected(make_error_code(ADSERR_CLIENT_SYNCRESINVALID));
        }

        co_return static_cast<size_t>(bytesRead);
    }

    auto TcAdsDllClient::writeFrom(std::string_view path,
                                   std::span<const std::byte> src,
                                   std::chrono::milliseconds timeout) -> coro::Task<result::Result<void>>
    {
        if (m_portHandle == 0) {
            co_return std::unexpected(make_error_code(ADSERR_CLIENT_PORTNOTOPEN));
        }

        auto timeoutStatus = setTimeout(timeout);
        if (timeoutStatus != ADSERR_NOERR) {
            co_return std::unexpected(make_error_code(timeoutStatus));
        }

        auto handleResult = createSymbolHandle(path);
        if (!handleResult) {
            co_return std::unexpected(handleResult.error());
        }

        const auto handle = handleResult.value();
        auto statusCode = AdsSyncWriteReqEx(m_portHandle,
                                            &m_remoteAddress,
                                            ADSIGRP_SYM_VALBYHND,
                                            handle,
                                            static_cast<unsigned long>(src.size()),
                                            const_cast<std::byte*>(src.data()));
        releaseSymbolHandle(handle);

        if (statusCode != ADSERR_NOERR) {
            co_return std::unexpected(make_error_code(statusCode));
        }

        co_return result::success();
    }

    void __stdcall TcAdsDllClient::NotificationCallback(AmsAddr* pAddr,
                                                        AdsNotificationHeader* pNotification,
                                                        unsigned long hUser)
    {
        if (hUser == 0) {
            return;
        }

        TcAdsDllClient* driver = nullptr;
        {
            std::scoped_lock lock(s_registryMutex);
            if (const auto it = s_registry.find(static_cast<uint32_t>(hUser)); it != s_registry.end()) {
                driver = it->second;
            }
        }

        if (driver) {
            driver->OnNotification(pNotification);
        }
    }

    void TcAdsDllClient::OnNotification(AdsNotificationHeader* pNotification)
    {
        std::shared_ptr<RawSubscription> stream;
        std::vector<std::byte> data;

        {
            std::scoped_lock lock(m_mutex);
            if (const auto it = m_subscriptionContexts.find(pNotification->hNotification);
                it != m_subscriptionContexts.end()) {
                stream = it->second.stream;
                const auto* dataPtr = reinterpret_cast<const std::byte*>(pNotification + 1);
                data.assign(dataPtr, dataPtr + pNotification->cbSampleSize);
            }
        }

        if (stream) {
            stream->stream.push(std::move(data));
        }
    }

    auto TcAdsDllClient::subscribeRaw(std::string_view path,
                                      size_t size,
                                      SubscriptionType type,
                                      std::chrono::milliseconds interval)
      -> coro::Task<result::Result<std::shared_ptr<RawSubscription>>>
    {
        if (m_portHandle == 0) {
            co_return std::unexpected(make_error_code(ADSERR_CLIENT_PORTNOTOPEN));
        }

        const auto handleResult = createSymbolHandle(path);
        if (!handleResult) {
            co_return std::unexpected(handleResult.error());
        }

        const auto symbolHandle = handleResult.value();
        AdsNotificationAttrib attrib{ .cbLength = static_cast<unsigned long>(size),
                                      .nTransMode = type == SubscriptionType::OnChange ? ADSTRANS_SERVERONCHA
                                                                                       : ADSTRANS_SERVERCYCLE,
                                      .nMaxDelay = 0,
                                      .nCycleTime = static_cast<unsigned long>(interval.count() * 10000) };

        unsigned long notificationHandle = 0;
        auto statusCode = AdsSyncAddDeviceNotificationReqEx(m_portHandle,
                                                            &m_remoteAddress,
                                                            ADSIGRP_SYM_VALBYHND,
                                                            symbolHandle,
                                                            &attrib,
                                                            &TcAdsDllClient::NotificationCallback,
                                                            m_driverId,
                                                            &notificationHandle);
        if (statusCode != ADSERR_NOERR) {
            releaseSymbolHandle(symbolHandle);
            co_return std::unexpected(make_error_code(statusCode));
        }

        auto rawSub = std::shared_ptr<RawSubscription>(new RawSubscription(notificationHandle),
                                                       [this](RawSubscription* p) {
            this->unsubscribeRawSync(p->id);
            delete p;
        });

        {
            std::scoped_lock lock(m_mutex);
            m_subscriptionContexts.emplace(notificationHandle,
                                           SubscriptionContext{ .symbolHandle = symbolHandle,
                                                                .notificationHandle = notificationHandle,
                                                                .stream = rawSub });
        }

        co_return rawSub;
    }

    auto TcAdsDllClient::unsubscribeRaw(std::shared_ptr<RawSubscription> subscription)
      -> coro::Task<result::Result<void>>
    {
        if (!subscription) {
            co_return result::success();
        }

        unsubscribeRawSync(subscription->id);
        co_return result::success();
    }

    auto TcAdsDllClient::unsubscribeRawSync(uint64_t id) -> void
    {
        std::shared_ptr<RawSubscription> stream;
        uint32_t symbolHandle = 0;
        uint32_t notificationHandle = 0;

        {
            std::scoped_lock lock(m_mutex);
            if (const auto it = m_subscriptionContexts.find(static_cast<uint32_t>(id));
                it != m_subscriptionContexts.end()) {
                stream = std::move(it->second.stream);
                symbolHandle = it->second.symbolHandle;
                notificationHandle = it->second.notificationHandle;
                m_subscriptionContexts.erase(it);
            }
        }

        if (!stream && symbolHandle == 0 && notificationHandle == 0) {
            return;
        }

        if (stream) {
            stream->stream.close();
        }

        if (m_portHandle != 0 && notificationHandle != 0) {
            (void)AdsSyncDelDeviceNotificationReqEx(m_portHandle, &m_remoteAddress, notificationHandle);
        }

        if (m_portHandle != 0 && symbolHandle != 0) {
            releaseSymbolHandle(symbolHandle);
        }
    }

    auto TcAdsDllClient::getTimeout() const -> std::chrono::milliseconds
    {
        if (m_portHandle == 0) {
            return std::chrono::milliseconds(0);
        }

        long timeoutMs = 0;
        if (AdsSyncGetTimeoutEx(m_portHandle, &timeoutMs) != ADSERR_NOERR) {
            return std::chrono::milliseconds(0);
        }

        return std::chrono::milliseconds(timeoutMs);
    }

    auto TcAdsDllClient::setTimeout(std::chrono::milliseconds timeout) const -> long
    {
        if (m_portHandle == 0) {
            return ADSERR_CLIENT_PORTNOTOPEN;
        }

        const auto timeoutMs = timeout.count() != 0 ? timeout.count() : m_defaultTimeout.count();
        return AdsSyncSetTimeoutEx(m_portHandle, static_cast<long>(timeoutMs));
    }

    auto TcAdsDllClient::createSymbolHandle(std::string_view path) -> result::Result<uint32_t>
    {
        if (m_portHandle == 0) {
            return std::unexpected(make_error_code(ADSERR_CLIENT_PORTNOTOPEN));
        }

        auto symbolPath = std::string(path);
        uint32_t handle = 0;
        unsigned long bytesRead = 0;
        const auto statusCode = AdsSyncReadWriteReqEx2(m_portHandle,
                                                       &m_remoteAddress,
                                                       ADSIGRP_SYM_HNDBYNAME,
                                                       0,
                                                       sizeof(handle),
                                                       &handle,
                                                       static_cast<unsigned long>(symbolPath.size() + 1),
                                                       symbolPath.data(),
                                                       &bytesRead);
        if (statusCode != ADSERR_NOERR) {
            return std::unexpected(make_error_code(statusCode));
        }

        if (bytesRead != sizeof(handle)) {
            return std::unexpected(make_error_code(ADSERR_CLIENT_SYNCRESINVALID));
        }

        return handle;
    }

    void TcAdsDllClient::releaseSymbolHandle(uint32_t handle)
    {
        if (m_portHandle == 0 || handle == 0) {
            return;
        }

        (void)AdsSyncWriteReqEx(
          m_portHandle, &m_remoteAddress, ADSIGRP_SYM_RELEASEHND, 0, sizeof(handle), &handle);
    }
}