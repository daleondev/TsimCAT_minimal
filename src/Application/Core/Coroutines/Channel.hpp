#pragma once

#include "Context.hpp"
#include "Task.hpp"

#include "Utils/memory_utils.hpp"
#include "Utils/queue_utils.hpp"

#include <list>
#include <vector>

namespace core::coro
{
    namespace detail
    {
        struct RawBinaryAwaiter;
    }

    enum class ChannelMode
    {
        Broadcast,
        LoadBalancer
    };

    class RawBinaryChannel
    {
      public:
        using Bytes = std::vector<std::byte>;

        auto setMode(ChannelMode mode) -> void
        {
            std::scoped_lock lock(m_state->mutex);
            m_state->mode = mode;
        }

        auto push(Bytes raw) -> void;
        auto close() -> void;

        auto next(std::optional<Bytes>& dest) -> detail::RawBinaryAwaiter;

      protected:
        struct Waiter
        {
            std::coroutine_handle<> handle{};
            IExecutor* executor{ nullptr };
            // For broadcast, we need a place to put the result
            std::optional<Bytes>* resultDest{ nullptr };
            std::weak_ptr<void> lifeToken{};
            detail::RawBinaryAwaiter* awaiterPtr{ nullptr };
        };

        struct State
        {
            std::mutex mutex{};
            std::deque<Bytes> queue{};
            bool closed{ false };
            std::list<Waiter> waiters{};
            ChannelMode mode{ ChannelMode::Broadcast };
        };

        std::shared_ptr<State> m_state{ std::make_shared<State>() };

        friend class detail::RawBinaryAwaiter;
        template<typename T>
            requires std::is_trivially_copyable_v<T>
        friend class BinaryChannel;
    };

    template<typename T>
        requires std::is_trivially_copyable_v<T>
    class BinaryChannel
    {
      public:
        BinaryChannel() = default;
        BinaryChannel(const RawBinaryChannel& raw)
          : m_state(raw.m_state)
        {
        }

        auto setMode(ChannelMode mode) -> void
        {
            if (m_state) {
                std::scoped_lock lock(m_state->mutex);
                m_state->mode = mode;
            }
        }

        auto next() -> Task<std::optional<T>>
        {
            RawBinaryChannel raw{};
            raw.m_state = m_state;

            std::optional<RawBinaryChannel::Bytes> result{};
            co_await raw.next(result);

            if (!result || result->size() != sizeof(T)) {
                co_return std::nullopt;
            }

            T val{};
            utils::memory::memcpy(val, result.value());
            co_return val;
        }

      private:
        std::shared_ptr<RawBinaryChannel::State> m_state;
    };

    template<typename T>
    class Channel
    {
      private:
        struct Waiter
        {
            std::coroutine_handle<> handle{};
            IExecutor* executor{ nullptr };
            std::optional<T>* dest{ nullptr };
            std::weak_ptr<void> lifeToken{};
        };

        struct State
        {
            std::mutex mutex{};
            std::deque<T> queue{};
            std::list<Waiter> waiters{};
            bool closed{ false };
        };

        struct Awaiter
        {
            std::shared_ptr<State> state;
            std::optional<T> result;

            auto await_ready() -> bool
            {
                std::scoped_lock lock(state->mutex);
                if (!state->queue.empty()) {
                    result = std::move(state->queue.front());
                    state->queue.pop_front();
                    return true;
                }
                return state->closed;
            }

            template<typename P>
            auto await_suspend(std::coroutine_handle<P> h) -> bool
            {
                std::scoped_lock lock(state->mutex);
                if (state->closed || !state->queue.empty()) {
                    return false;
                }

                IExecutor* ex = nullptr;
                std::weak_ptr<void> token;
                if constexpr (requires { h.promise().executor; }) {
                    ex = h.promise().executor;
                    if (ex) {
                        token = ex->getLifeToken();
                    }
                }

                state->waiters.push_back({ h, ex, &result, token });
                return true;
            }

            auto await_resume() -> std::optional<T> { return std::move(result); }
        };

      public:
        auto push(T val) -> void
        {
            std::unique_lock lock(m_state->mutex);
            if (m_state->closed) {
                return;
            }

            if (m_state->waiters.empty()) {
                m_state->queue.push_back(std::move(val));
                return;
            }

            auto waiter = std::move(m_state->waiters.front());
            m_state->waiters.pop_front();

            if (waiter.dest) {
                *waiter.dest = std::move(val);
            }

            lock.unlock();

            if (waiter.executor) {
                if (auto token = waiter.lifeToken.lock()) {
                    waiter.executor->schedule(waiter.handle);
                }
            }
            else {
                waiter.handle.resume();
            }
        }

        auto close() -> void
        {
            std::unique_lock lock(m_state->mutex);
            if (m_state->closed) {
                return;
            }
            m_state->closed = true;
            auto waiters = std::move(m_state->waiters);
            lock.unlock();

            for (auto& w : waiters) {
                if (w.executor) {
                    if (auto token = w.lifeToken.lock()) {
                        w.executor->schedule(w.handle);
                    }
                }
                else {
                    w.handle.resume();
                }
            }
        }

        auto next() -> Task<std::optional<T>> { co_return co_await Awaiter{ m_state }; }

      private:
        std::shared_ptr<State> m_state{ std::make_shared<State>() };
    };

    namespace detail
    {
        struct RawBinaryAwaiter
        {
            std::shared_ptr<RawBinaryChannel::State> state{};
            std::optional<RawBinaryChannel::Bytes>& dest;
            std::optional<std::list<RawBinaryChannel::Waiter>::iterator> m_iterator{};

            ~RawBinaryAwaiter()
            {
                if (m_iterator) {
                    std::scoped_lock lock(state->mutex);
                    if (m_iterator) {
                        state->waiters.erase(*m_iterator);
                    }
                }
            }

            void unlink() { m_iterator = std::nullopt; }

            auto await_ready() -> bool
            {
                std::scoped_lock lock(state->mutex);
                if (auto raw{ utils::queue::pop(state->queue) }) {
                    dest.emplace(raw.value());
                    return true;
                }
                return state->closed;
            }

            template<typename P>
            auto await_suspend(std::coroutine_handle<P> handle) -> bool
            {
                std::scoped_lock lock(state->mutex);

                if (state->closed || !state->queue.empty()) {
                    return false;
                }

                IExecutor* executor{ nullptr };
                std::weak_ptr<void> lifeToken;

                if constexpr (requires { handle.promise().executor; }) {
                    executor = handle.promise().executor;
                    if (executor) {
                        lifeToken = executor->getLifeToken();
                    }
                }

                m_iterator = state->waiters.insert(state->waiters.end(),
                                                   { handle, executor, &dest, std::move(lifeToken), this });
                return true;
            }

            auto await_resume() -> void
            {
                // Result is already in 'dest' or dest is nullopt (closed)
                // If we resumed normally, 'unlink()' was already called by 'push'.
            }
        };

        static auto resumeWaiter(auto& waiter) -> void
        {
            if (waiter.executor) {
                if (auto token = waiter.lifeToken.lock()) {
                    waiter.executor->schedule(waiter.handle);
                }
            }
            else {
                waiter.handle.resume();
            }
        }
    }

    inline auto RawBinaryChannel::next(std::optional<Bytes>& dest) -> detail::RawBinaryAwaiter
    {
        return detail::RawBinaryAwaiter{ m_state, dest };
    }

    inline auto RawBinaryChannel::push(Bytes raw) -> void
    {
        std::unique_lock lock(m_state->mutex);
        if (m_state->closed) {
            return;
        }

        if (m_state->waiters.empty()) {
            // store until new waiter spawns
            m_state->queue.push_back(std::move(raw));
            return;
        }

        if (m_state->mode == ChannelMode::LoadBalancer) {
            auto waiter{ std::move(m_state->waiters.front()) };
            m_state->waiters.pop_front();

            // detach from awaiter so it doesnt try to erase itself on destruction
            if (waiter.awaiterPtr) {
                waiter.awaiterPtr->unlink();
            }

            if (waiter.resultDest) {
                waiter.resultDest->emplace(std::move(raw));
            }

            lock.unlock();
            detail::resumeWaiter(waiter);
        }
        else {
            auto toResume{ std::move(m_state->waiters) };
            m_state->waiters.clear();

            for (auto& waiter : toResume) {
                // detach from awaiter so it doesnt try to erase itself on destruction
                if (waiter.awaiterPtr) {
                    waiter.awaiterPtr->unlink();
                }

                if (waiter.resultDest) {
                    waiter.resultDest->emplace(raw);
                }
            }

            lock.unlock();
            for (auto& waiter : toResume) {
                detail::resumeWaiter(waiter);
            }
        }
    }

    inline auto RawBinaryChannel::close() -> void
    {
        std::list<Waiter> toResume;
        {
            std::scoped_lock lock(m_state->mutex);
            if (m_state->closed) {
                return;
            }
            m_state->closed = true;
            toResume = std::move(m_state->waiters);
        }

        for (auto& waiter : toResume) {
            if (waiter.awaiterPtr) {
                waiter.awaiterPtr->unlink();
            }
            detail::resumeWaiter(waiter);
        }
    }
}