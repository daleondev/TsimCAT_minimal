#pragma once

#include "ISymbolicLink.hpp"

#include "Utils/memory_utils.hpp"

#include <open62541.h>

#include <thread>
#include <unordered_map>

namespace core::link::symbolic
{
    class OpcUaClient
      : public IClient
      , public ISymbolicLink
    {
      public:
        explicit OpcUaClient(std::string endpointUrl);
        ~OpcUaClient() override;

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

        inline operator UA_Client*() { return m_client.get(); }

      private:
        auto handleChannelState(UA_SecureChannelState state) -> void;
        auto handleSessionState(UA_SessionState state) -> void;
        auto worker() -> void;

        static void dataChangeNotificationCallback(UA_Client* client,
                                                   UA_UInt32 subId,
                                                   void* subContext,
                                                   UA_UInt32 monId,
                                                   void* monContext,
                                                   UA_DataValue* value);
        void handleDataChange(UA_UInt32 monId, UA_DataValue* value);

        struct MonitoredItemInfo
        {
            uint32_t subscriptionId;
            std::shared_ptr<RawSubscription> stream;
            bool isPolling{ false };
            std::string path;
            std::chrono::milliseconds interval;
            std::chrono::steady_clock::time_point nextPoll;
        };

        auto doPoll(MonitoredItemInfo& info) -> void;

        std::string m_endpointUrl;
        bool m_connected{ false };
        bool m_sessionActive{ false };

        std::unordered_map<uint32_t, MonitoredItemInfo> m_monitoredItems;
        std::unordered_map<int64_t, uint32_t> m_subscriptionMap;

        std::unique_ptr<UA_Client, utils::memory::Deleter<UA_Client_delete>> m_client;

        std::recursive_mutex m_mutex;
        std::atomic<bool> m_workerRunning{ false };
        std::jthread m_worker;
    };

}