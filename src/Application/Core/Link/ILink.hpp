#pragma once

#include "Common/Result.hpp"
#include "Coroutines/Task.hpp"

#include <chrono>

namespace core::link
{
    static constexpr std::chrono::milliseconds NO_TIMEOUT{ std::chrono::milliseconds(0) };

    enum class Status { Disconnected, Connecting, Connected, Faulty };
    enum class Role { Server, Client };
    enum class Mode { Raw, Symbolic };
    enum class Protocol { Tcp, Ads, OpcUa };

    class IServer;
    class IClient;
    class IRawLink;
    class ISymbolicLink;

    class ILink
    {
      public:
        virtual ~ILink() = default;

        virtual auto getRole() const -> Role = 0;
        virtual auto getMode() const -> Mode = 0;
        virtual auto status() const -> Status = 0;

        virtual auto asServer() -> IServer* { return nullptr; }
        virtual auto asClient() -> IClient* { return nullptr; }
        virtual auto asRaw() -> IRawLink* { return nullptr; }
        virtual auto asSymbolic() -> ISymbolicLink* { return nullptr; }
    };

    class IServer : virtual public ILink
    {
      public:
        auto asServer() -> IServer* override { return this; }
        auto getRole() const -> Role override { return Role::Server; }

        virtual auto start() -> result::Result<void> = 0;
        virtual auto stop() -> result::Result<void> = 0;
        virtual auto accept(std::chrono::milliseconds timeout = NO_TIMEOUT)
          -> coro::Task<result::Result<void>> = 0;
    };

    class IClient : virtual public ILink
    {
      public:
        auto asClient() -> IClient* override { return this; }
        auto getRole() const -> Role override { return Role::Client; }

        virtual auto connect(std::chrono::milliseconds timeout = NO_TIMEOUT)
          -> coro::Task<result::Result<void>> = 0;
        virtual auto disconnect(std::chrono::milliseconds timeout = NO_TIMEOUT)
          -> coro::Task<result::Result<void>> = 0;
    };
}