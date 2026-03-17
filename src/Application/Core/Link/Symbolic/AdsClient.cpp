#include "AdsClient.hpp"
#include "Coroutines/Context.hpp"

#include "format_utils.hpp"

#include <atomic>
#include <cassert>
#include <iostream>
#include <mutex>
#include <print>
#include <unordered_map>

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

    using AdsErrorType = std::remove_cvref_t<decltype(AdsException::errorCode)>;
    enum class AdsError : AdsErrorType
    {
        None = 0x00, // ADSERR_NOERR

        // ADSSTATE
        AdsStateInvalid = 0,
        AdsStateIdle = 1,
        AdsStateReset = 2,
        AdsStateInit = 3,
        AdsStateStart = 4,
        AdsStateRun = 5,
        AdsStateStop = 6,
        AdsStateSavecfg = 7,
        AdsStateLoadcfg = 8,
        AdsStatePowerfailur = 9,
        AdsStatePowergood = 10,
        AdsStateError = 11,
        AdsStateShutdown = 12,
        AdsStateSuspend = 13,
        AdsStateResume = 14,
        AdsStateConfig = 15,
        AdsStateReconfig = 16,
        AdsStateStopping = 17,
        AdsStateIncompatible = 18,
        AdsStateException = 19,

        // --- Device Errors (Base: 0x0700) ---
        DeviceError = 0x700,                /**< Error class < device error > */
        DeviceServiceNotSupported = 0x701,  /**< Service is not supported by server */
        DeviceInvalidGroup = 0x702,         /**< invalid indexGroup */
        DeviceInvalidOffset = 0x703,        /**< invalid indexOffset */
        DeviceInvalidAccess = 0x704,        /**< reading/writing not permitted */
        DeviceInvalidSize = 0x705,          /**< parameter size not correct */
        DeviceInvalidData = 0x706,          /**< invalid parameter value(s) */
        DeviceNotReady = 0x707,             /**< device is not in a ready state */
        DeviceBusy = 0x708,                 /**< device is busy */
        DeviceInvalidContext = 0x709,       /**< invalid context (must be InWindows) */
        DeviceNoMemory = 0x70A,             /**< out of memory */
        DeviceInvalidParam = 0x70B,         /**< invalid parameter value(s) */
        DeviceNotFound = 0x70C,             /**< not found (files, ...) */
        DeviceSyntaxError = 0x70D,          /**< syntax error in command or file */
        DeviceIncompatible = 0x70E,         /**< objects do not match */
        DeviceExists = 0x70F,               /**< object already exists */
        DeviceSymbolNotFound = 0x710,       /**< symbol not found */
        DeviceSymbolVersionInvalid = 0x711, /**< symbol version invalid, possibly caused by 'onlinechange' */
        DeviceInvalidState = 0x712,         /**< server is in invalid state */
        DeviceTransModeNotSupp = 0x713,     /**< AdsTransMode not supported */
        DeviceNotifyHandleInvalid = 0x714,  /**< Notification handle is invalid */
        DeviceClientUnknown = 0x715,        /**< Notification client not registered */
        DeviceNoMoreHandles = 0x716,        /**< no more notification handles */
        DeviceInvalidWatchSize = 0x717,     /**< size for watch too big */
        DeviceNotInitialized = 0x718,       /**< device not initialized */
        DeviceTimeout = 0x719,              /**< device has a timeout */
        DeviceNoInterface = 0x71A,          /**< query interface failed */
        DeviceInvalidInterface = 0x71B,     /**< wrong interface required */
        DeviceInvalidClsId = 0x71C,         /**< class ID is invalid */
        DeviceInvalidObjId = 0x71D,         /**< object ID is invalid */
        DevicePending = 0x71E,              /**< request is pending */
        DeviceAborted = 0x71F,              /**< request is aborted */
        DeviceWarning = 0x720,              /**< signal warning */
        DeviceInvalidArrayIndex = 0x721,    /**< invalid array index */
        DeviceSymbolNotActive = 0x722,      /**< symbol not active */
        DeviceAccessDenied = 0x723,         /**< access denied */
        DeviceLicenseNotFound = 0x724,      /**< no license found */
        DeviceLicenseExpired = 0x725,       /**< license expired */
        DeviceLicenseExceeded = 0x726,      /**< license exceeded */
        DeviceLicenseInvalid = 0x727,       /**< license invalid */
        DeviceLicenseSystemId = 0x728,      /**< license invalid system id */
        DeviceLicenseNoTimeLimit = 0x729,   /**< license not time limited */
        DeviceLicenseFutureIssue = 0x72A,   /**< license issue time in the future */
        DeviceLicenseTimeTooLong = 0x72B,   /**< license time period too long */
        DeviceException = 0x72C,            /**< exception in device specific code */
        DeviceLicenseDuplicated = 0x72D,    /**< license file read twice */
        DeviceSignatureInvalid = 0x72E,     /**< invalid signature */
        DeviceCertificateInvalid = 0x72F,   /**< public key certificate */

        // --- Client Errors (Base: 0x0740) ---
        ClientError = 0x740,             /**< Error class < client error > */
        ClientInvalidParam = 0x741,      /**< invalid parameter at service call */
        ClientListEmpty = 0x742,         /**< polling list is empty */
        ClientVarUsed = 0x743,           /**< var connection already in use */
        ClientDuplicateInvokeId = 0x744, /**< invoke id in use */
        ClientSyncTimeout = 0x745,       /**< timeout elapsed */
        ClientW32Error = 0x746,          /**< error in win32 subsystem */
        ClientTimeoutInvalid = 0x747,    /**< Invalid client timeout value */
        ClientPortNotOpen = 0x748,       /**< ads dll */
        ClientNoAmsAddr = 0x749,         /**< ads dll */
        ClientSyncInternal = 0x750,      /**< internal error in ads sync */
        ClientAddHash = 0x751,           /**< hash table overflow */
        ClientRemoveHash = 0x752,        /**< key not found in hash table */
        ClientNoMoreSym = 0x753,         /**< no more symbols in cache */
        ClientSyncResInvalid = 0x754,    /**< invalid response received */
        ClientSyncPortLocked = 0x755,    /**< sync port is locked */

        Unknown = std::numeric_limits<AdsErrorType>::max()
    };

    class AdsErrorCategory : public std::error_category
    {
      public:
        const char* name() const noexcept override { return "AdsError"; }

        std::string message(int ev) const override
        {
            static constexpr auto enums{ fmtu::detail::underlying_enumerators<AdsError>() };
            if (!std::ranges::contains(enums, static_cast<AdsErrorType>(ev))) {
                return "Unknown";
            }
            return std::to_string(static_cast<AdsErrorType>(ev));
        }
    };

    const std::error_category& ads_category()
    {
        static AdsErrorCategory instance;
        return instance;
    }

    std::error_code make_error_code(AdsError e)
    {
        return std::error_code(static_cast<int>(e), ads_category());
    }

    static AdsError handleException(const std::exception& ex)
    {
        auto* adsEx{ dynamic_cast<const AdsException*>(&ex) };
        if (adsEx) {
            std::println(std::cerr, "Ads Exception: {}", adsEx->what());
            return static_cast<AdsError>(adsEx->errorCode);
        }
        std::println(std::cerr, "Unknown Exception: {}", ex.what());
        return AdsError::Unknown;
    }

    // Registry for safe 64-bit -> 32-bit callback handling
    static std::mutex s_registryMutex;
    static std::unordered_map<uint32_t, core::link::symbolic::AdsClient*> s_registry;
    static std::atomic<uint32_t> s_nextDriverId{ 1 };
}

namespace std
{
    template<>
    struct is_error_code_enum<AdsError> : true_type
    {
    };
}

namespace core::link::symbolic
{

    AdsClient::AdsClient(std::string_view remoteNetId,
                         std::string ipAddress,
                         uint16_t port,
                         std::string_view localNetId)
      : m_remoteNetId{ strToNetId(remoteNetId) }
      , m_ipAddress(std::move(ipAddress))
      , m_port{ port }
      , m_route{ nullptr }
      , m_defaultTimeout{}
      , m_driverId{ s_nextDriverId++ }
    {
        if (!localNetId.empty()) {
            bhf::ads::SetLocalAddress(strToNetId(localNetId));
        }

        {
            std::scoped_lock lock(s_registryMutex);
            s_registry[m_driverId] = this;
        }
    }

    AdsClient::~AdsClient()
    {
        (void)disconnect(std::chrono::milliseconds(0));

        {
            std::scoped_lock lock(s_registryMutex);
            s_registry.erase(m_driverId);
        }
    }

    auto AdsClient::connect(std::chrono::milliseconds timeout) -> coro::Task<result::Result<void>>
    {
        co_return co_await coro::runAsync<result::Result<void>>([this, timeout]() {
            auto err{ AdsError::None };
            try {
                m_route = std::make_unique<AdsDevice>(m_ipAddress, m_remoteNetId, m_port);

                m_defaultTimeout = getTimeout();
                m_route->SetTimeout(timeout.count());

                (void)m_route->GetDeviceInfo();
            } catch (const std::exception& ex) {
                err = handleException(ex);
                m_route.reset();
            }

            return err == AdsError::None ? result::success() : std::unexpected(make_error_code(err));
        });
    }

    auto AdsClient::status() const -> Status { return m_route ? Status::Connected : Status::Disconnected; }

    auto AdsClient::disconnect(std::chrono::milliseconds timeout) -> coro::Task<result::Result<void>>
    {
        if (!m_route) {
            co_return result::success();
        }

        auto err{ AdsError::None };
        try {
            setTimeout(timeout);

            {
                std::scoped_lock lock(m_mutex);
                for (auto& [id, context] : m_subscriptionContexts) {
                    if (context.stream) {
                        context.stream->stream.close();
                    }
                }
                m_subscriptionContexts.clear();
            }

            m_route.reset();
        } catch (const std::exception& ex) {
            err = handleException(ex);
        }

        co_return err == AdsError::None ? result::success() : std::unexpected(make_error_code(err));
    }

    auto AdsClient::readInto(std::string_view path,
                             std::span<std::byte> dest,
                             std::chrono::milliseconds timeout) -> coro::Task<result::Result<size_t>>
    {
        uint32_t bytesRead = 0;
        auto err{ AdsError::None };
        try {
            setTimeout(timeout);

            auto handle{ m_route->GetHandle(std::string(path)) };
            err = static_cast<AdsError>(
              m_route->ReadReqEx2(ADSIGRP_SYM_VALBYHND, *handle, dest.size(), dest.data(), &bytesRead));
        } catch (const std::exception& ex) {
            err = handleException(ex);
        }

        if (err != AdsError::None) {
            co_return std::unexpected(make_error_code(err));
        }

        if (bytesRead != dest.size()) {
            co_return std::unexpected(make_error_code(AdsError::Unknown));
        }

        co_return bytesRead;
    }

    auto AdsClient::writeFrom(std::string_view path,
                              std::span<const std::byte> src,
                              std::chrono::milliseconds timeout) -> coro::Task<result::Result<void>>
    {
        auto err{ AdsError::None };
        try {
            setTimeout(timeout);

            auto handle{ m_route->GetHandle(std::string(path)) };
            err = static_cast<AdsError>(
              m_route->WriteReqEx(ADSIGRP_SYM_VALBYHND, *handle, src.size(), src.data()));
        } catch (const std::exception& ex) {
            err = handleException(ex);
        }

        co_return err == AdsError::None ? result::success() : std::unexpected(make_error_code(err));
    }

    void AdsClient::NotificationCallback(const AmsAddr* pAddr,
                                         const AdsNotificationHeader* pNotification,
                                         uint32_t hUser)
    {
        if (!hUser)
            return;

        AdsClient* driver = nullptr;
        {
            std::scoped_lock lock(s_registryMutex);
            if (auto it = s_registry.find(hUser); it != s_registry.end()) {
                driver = it->second;
            }
        }

        if (driver) {
            driver->OnNotification(pNotification);
        }
    }

    void AdsClient::OnNotification(const AdsNotificationHeader* pNotification)
    {
        std::shared_ptr<RawSubscription> stream;
        std::vector<std::byte> data;

        {
            std::scoped_lock lock(m_mutex);
            if (auto it = m_subscriptionContexts.find(pNotification->hNotification);
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

    auto AdsClient::subscribeRaw(std::string_view path,
                                 size_t size,
                                 SubscriptionType type,
                                 std::chrono::milliseconds interval)
      -> coro::Task<result::Result<std::shared_ptr<RawSubscription>>>
    {
        uint32_t transMode{ static_cast<uint32_t>(
          (type == SubscriptionType::OnChange) ? ADSTRANS_SERVERONCHA : ADSTRANS_SERVERCYCLE) };
        uint32_t cycleTime{ static_cast<uint32_t>(interval.count() * 10000) };

        AdsNotificationAttrib attrib{ .cbLength = static_cast<uint32_t>(size),
                                      .nTransMode = transMode,
                                      .nMaxDelay = 0,
                                      .nCycleTime = cycleTime };

        auto err{ AdsError::None };
        try {
            auto symbolHandle{ m_route->GetHandle(std::string(path)) };
            auto notificationHandle{ m_route->GetHandle(
              ADSIGRP_SYM_VALBYHND, *symbolHandle, attrib, &AdsClient::NotificationCallback, m_driverId) };
            auto id{ *notificationHandle };

            std::scoped_lock lock(m_mutex);
            // Custom deleter to ensure we unsubscribe ONLY when the last reference dies
            auto rawSub =
              std::shared_ptr<RawSubscription>(new RawSubscription(id), [this](RawSubscription* p) {
                this->unsubscribeRawSync(p->id);
                delete p;
            });

            m_subscriptionContexts.emplace(
              id,
              SubscriptionContext{ .symbolHandle = std::move(symbolHandle),
                                   .notificationHandle = std::move(notificationHandle),
                                   .stream = rawSub });

            co_return rawSub;
        } catch (const std::exception& ex) {
            err = handleException(ex);
        }

        co_return std::unexpected(make_error_code(err));
    }

    auto AdsClient::unsubscribeRaw(std::shared_ptr<RawSubscription> subscription)
      -> coro::Task<result::Result<void>>
    {
        if (!subscription) {
            co_return result::success();
        }
        // Just clearing our internal reference is enough, the deleter handles the rest
        // when the user's reference also goes away.
        unsubscribeRawSync(subscription->id);
        co_return result::success();
    }

    auto AdsClient::unsubscribeRawSync(uint64_t id) -> void
    {
        std::scoped_lock lock(m_mutex);
        if (auto it = m_subscriptionContexts.find(static_cast<uint32_t>(id));
            it != m_subscriptionContexts.end()) {
            if (it->second.stream) {
                it->second.stream->stream.close();
            }
            m_subscriptionContexts.erase(it);
        }
    }

    auto AdsClient::getTimeout() -> std::chrono::milliseconds
    {
        return std::chrono::milliseconds(m_route->GetTimeout());
    }

    auto AdsClient::setTimeout(std::chrono::milliseconds timeout) -> void
    {
        auto ms{ timeout.count() };
        m_route->SetTimeout(ms ? ms : m_defaultTimeout.count());
    }

}