#include "TcpServer.hpp"
#include "Coroutines/Context.hpp"

#include "format_utils.hpp"

#include <optional>
#include <thread>
#include <variant>

namespace
{
    template<typename T>
    struct AsioAwaiter
    {
        asio::io_context& ctx;
        asio::awaitable<T> task;
        std::optional<T> result;
        std::exception_ptr ex;

        bool await_ready() { return false; }
        void await_suspend(std::coroutine_handle<> h)
        {
            asio::co_spawn(ctx, std::move(task), [this, h](std::exception_ptr e, T r) mutable {
                if (e)
                    ex = e;
                else
                    result = std::move(r);
                h.resume();
            });
        }
        T await_resume()
        {
            if (ex)
                std::rethrow_exception(ex);
            return std::move(*result);
        }
    };
}

namespace core::link::raw
{

    TcpServer::TcpServer(uint16_t port)
      : m_acceptor{ m_context, asio::ip::tcp::endpoint{ asio::ip::address_v4(), port } }
      , m_socket{ m_context }
    {
    }

    TcpServer::~TcpServer()
    {
        (void)stop();
    }

    auto TcpServer::start() -> result::Result<void>
    {
        if (m_thread.joinable()) {
            return result::success();
        }

        m_status = Status::Disconnected;
        // Capture work guard to keep run() alive
        m_thread = std::thread([this, work = asio::make_work_guard(m_context)]() { m_context.run(); });

        return result::success();
    }

    auto TcpServer::stop() -> result::Result<void>
    {
        m_status = Status::Disconnected;
        if (!m_context.stopped()) {
            m_context.stop();
        }
        if (m_thread.joinable()) {
            m_thread.join();
        }
        if (m_socket.is_open()) {
            m_socket.close();
        }
        
        // Reset context for potential restart
        m_context.restart();
        return result::success();
    }

    auto TcpServer::accept(std::chrono::milliseconds timeout) -> coro::Task<result::Result<void>>
    {
        if (m_socket.is_open()) {
            asio::error_code ec;
            m_socket.close(ec);
        }

        m_status = Status::Connecting;
        auto res = co_await AsioAwaiter<result::Result<void>>{
            m_context,
            [&]() -> asio::awaitable<result::Result<void>> {
                // If timeout > 0, we need a timer.
                // However, commonly accept might block indefinitely.
                // Using timeout if provided.

                using namespace asio::experimental::awaitable_operators;
                
                try {
                    if (timeout != NO_TIMEOUT) {
                        asio::steady_timer timer(co_await asio::this_coro::executor);
                        timer.expires_after(timeout);
                        
                        // We need a wrapper to match return types or handle the variant
                        auto result = co_await (m_acceptor.async_accept(m_socket, asio::use_awaitable) ||
                                                timer.async_wait(asio::use_awaitable));
                        
                        if (result.index() == 0) {
                            co_return result::success();
                        } else {
                            co_return std::unexpected(make_error_code(asio::error::timed_out));
                        }
                    } else {
                        co_await m_acceptor.async_accept(m_socket, asio::use_awaitable);
                        co_return result::success();
                    }
                } catch (const std::exception& ex) {
                    // map asio error?
                     co_return std::unexpected(make_error_code(asio::error::basic_errors::connection_aborted));
                }
            }()
        };

        if (res) {
            m_status = Status::Connected;
        } else {
            m_status = Status::Faulty; 
        }
        co_return res;
    }

    auto TcpServer::status() const -> Status
    {
        if (!m_socket.is_open()) {
            return Status::Disconnected;
        }
        return m_status;
    }

    auto TcpServer::receiveInto(std::string_view path,
                                std::span<std::byte> dest,
                                std::chrono::milliseconds timeout) -> coro::Task<result::Result<size_t>>
    {
        if (!m_socket.is_open()) {
            co_return std::unexpected(make_error_code(asio::error::not_connected));
        }

        co_return co_await AsioAwaiter<result::Result<size_t>>{
            m_context,
            [&]() -> asio::awaitable<result::Result<size_t>> {
                try {
                    if (timeout != NO_TIMEOUT) {
                        asio::steady_timer timer(co_await asio::this_coro::executor);
                        timer.expires_after(timeout);

                        using namespace asio::experimental::awaitable_operators;
                        auto result = co_await (m_socket.async_read_some(asio::buffer(dest), asio::use_awaitable) ||
                                                timer.async_wait(asio::use_awaitable));

                        if (result.index() == 0) {
                            co_return std::get<0>(result);
                        }
                        co_return std::unexpected(make_error_code(asio::error::timed_out));
                    } else {
                        auto result = co_await m_socket.async_read_some(asio::buffer(dest), asio::use_awaitable);
                        co_return result;
                    }
                } catch (const asio::system_error& ex) {
                    co_return std::unexpected(ex.code());
                } catch (...) {
                    co_return std::unexpected(make_error_code(asio::error::basic_errors::network_down));
                }
            }()
        };
    }

    auto TcpServer::sendFrom(std::string_view path,
                             std::span<const std::byte> src,
                             std::chrono::milliseconds timeout) -> coro::Task<result::Result<void>>
    {
        if (!m_socket.is_open()) {
            co_return std::unexpected(make_error_code(asio::error::not_connected));
        }

        co_return co_await AsioAwaiter<result::Result<void>>{
            m_context,
            [&]() -> asio::awaitable<result::Result<void>> {
                try {
                    if (timeout != NO_TIMEOUT) {
                        asio::steady_timer timer(co_await asio::this_coro::executor);
                        timer.expires_after(timeout);
                        using namespace asio::experimental::awaitable_operators;
                        auto result = co_await (asio::async_write(m_socket, asio::buffer(src), asio::use_awaitable) ||
                                                timer.async_wait(asio::use_awaitable));
                        if (result.index() == 0) {
                            co_return result::success();
                        }
                        co_return std::unexpected(make_error_code(asio::error::timed_out));
                    } else {
                        co_await asio::async_write(m_socket, asio::buffer(src), asio::use_awaitable);
                        co_return result::success();
                    }
                } catch (const asio::system_error& ex) {
                    co_return std::unexpected(ex.code());
                } catch (...) {
                    co_return std::unexpected(make_error_code(asio::error::basic_errors::network_down));
                }
            }()
        };
    }
}