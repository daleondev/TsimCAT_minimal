#pragma once

#include "IRawLink.hpp"

#include <asio.hpp>
#include <asio/experimental/awaitable_operators.hpp>

namespace core::link::raw
{
    class TcpServer
      : public IServer
      , public IRawLink
    {
      public:
        TcpServer(uint16_t port);
        ~TcpServer() override;

        auto start() -> result::Result<void> override;
        auto stop() -> result::Result<void> override;
        auto accept(std::chrono::milliseconds timeout = NO_TIMEOUT)
          -> coro::Task<result::Result<void>> override;

        auto status() const -> Status override;

        auto receiveInto(std::string_view path,
                         std::span<std::byte> dest,
                         std::chrono::milliseconds timeout = NO_TIMEOUT)
          -> coro::Task<result::Result<size_t>> override;

        auto sendFrom(std::string_view path,
                      std::span<const std::byte> src,
                      std::chrono::milliseconds timeout = NO_TIMEOUT)
          -> coro::Task<result::Result<void>> override;

      private:
        asio::io_context m_context;
        asio::ip::tcp::acceptor m_acceptor;
        asio::ip::tcp::socket m_socket;
        std::atomic<Status> m_status{ Status::Disconnected };
        std::thread m_thread;
    };
}
