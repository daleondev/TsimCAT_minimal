#pragma once

#include "Link/ILink.hpp"

#include "Common/Result.hpp"

#include "Coroutines/Task.hpp"

#include <span>
#include <string_view>

namespace core::link
{
    class IRawLink : virtual public ILink
    {
      public:
        auto asRaw() -> IRawLink* override { return this; }
        auto getMode() const -> Mode override { return Mode::Raw; }

        virtual auto receiveInto(std::string_view path,
                                 std::span<std::byte> dest,
                                 std::chrono::milliseconds timeout = NO_TIMEOUT)
          -> coro::Task<result::Result<size_t>> = 0;

        virtual auto sendFrom(std::string_view path,
                              std::span<const std::byte> src,
                              std::chrono::milliseconds timeout = NO_TIMEOUT)
          -> coro::Task<result::Result<void>> = 0;

        template<typename T>
        auto receive(std::string_view path, std::chrono::milliseconds timeout = NO_TIMEOUT)
          -> coro::Task<result::Result<T>>
        {
            T value{};
            auto res{ co_await receiveInto(path, std::as_writable_bytes(std::span{ &value, 1 }), timeout) };
            if (!res) {
                co_return std::unexpected(res.error());
            }
            co_return value;
        }

        auto send(std::string_view path, const auto& value, std::chrono::milliseconds timeout = NO_TIMEOUT)
          -> coro::Task<result::Result<void>>
        {
            co_return co_await sendFrom(path, std::as_bytes(std::span{ &value, 1 }), timeout);
        }
    };
}