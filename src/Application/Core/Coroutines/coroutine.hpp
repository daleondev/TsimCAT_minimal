#pragma once

#include "Channel.hpp"
#include "Context.hpp"
#include "Task.hpp"

#include <functional>

namespace core::coro
{
    namespace detail
    {
        template<typename Ex, typename Coro>
        auto co_spawn_impl(Ex& ex, Coro coro) -> DetachedTask
        {
            co_await std::invoke(std::move(coro), ex);
        }
    }

    template<Executor Ex, std::invocable<Ex&> Coro>
    auto co_spawn(Ex& ex, Coro&& coro) -> void
    {
        auto detached{ detail::co_spawn_impl(ex, std::forward<Coro>(coro)) };
        ex.schedule(detached.getHandle());
    }
}