#pragma once

#include "Context.hpp"

#include <exception>
#include <utility>
#include <future>
#include <thread>
#include <optional>

namespace core::coro
{
    // 1. Forward Declarations
    namespace detail
    {
        struct DetachedTaskPromise;
        template<typename T>
        struct TaskPromise;
    }

    template<typename T>
    class Task;

    class DetachedTask
    {
      public:
        using promise_type = detail::DetachedTaskPromise;
        using handle_type = std::coroutine_handle<promise_type>;

        DetachedTask(handle_type handle) : m_handle{ handle } {}
        ~DetachedTask() {}

        DetachedTask(const DetachedTask&) = delete;
        auto operator=(const DetachedTask&) -> DetachedTask& = delete;
        DetachedTask(DetachedTask&& other) noexcept : m_handle{ std::exchange(other.m_handle, nullptr) } {}
        auto operator=(DetachedTask&& other) noexcept -> DetachedTask& { m_handle = std::exchange(other.m_handle, nullptr); return *this; }

      private:
        handle_type m_handle;
    };

    // 2. Task Class Definition
    template<typename T>
    class Task
    {
      public:
        using promise_type = detail::TaskPromise<T>;
        using handle_type = std::coroutine_handle<promise_type>;

        Task(handle_type handle) : m_handle{ handle } {}
        ~Task() { if (m_handle) m_handle.destroy(); }

        Task(const Task&) = delete;
        auto operator=(const Task&) -> Task& = delete;

        Task(Task&& other) noexcept : m_handle{ std::exchange(other.m_handle, nullptr) } {}
        auto operator=(Task&& other) noexcept -> Task& {
            if (this != &other) { if (m_handle) m_handle.destroy(); m_handle = std::exchange(other.m_handle, nullptr); }
            return *this;
        }

        auto await_ready() const noexcept -> bool { return !m_handle || m_handle.done(); }
        auto await_suspend(std::coroutine_handle<> waiter) noexcept -> std::coroutine_handle<> {
            m_handle.promise().waiter = waiter;
            return m_handle;
        }
        auto await_resume() -> T {
            if (m_handle.promise().exception) std::rethrow_exception(m_handle.promise().exception);
            if constexpr (!std::is_void_v<T>) return std::move(*m_handle.promise().value);
        }

        auto getHandle() const -> const handle_type& { return m_handle; }

      private:
        handle_type m_handle;
    };

    // 3. Promise Definitions
    namespace detail
    {
        struct DetachedTaskPromise
        {
            auto get_return_object() -> DetachedTask;
            auto initial_suspend() -> std::suspend_always { return {}; }
            auto final_suspend() noexcept -> std::suspend_never { return {}; }
            auto unhandled_exception() -> void { std::terminate(); }
            auto return_void() -> void {}
        };

        template<typename T>
        struct TaskPromiseBase
        {
            std::coroutine_handle<> waiter{};
            std::exception_ptr exception{};
            IExecutor* executor{ nullptr };

            auto initial_suspend() -> std::suspend_always { return {}; }
            auto final_suspend() noexcept
            {
                struct awaiter
                {
                    bool await_ready() noexcept { return false; }
                    std::coroutine_handle<> await_suspend(std::coroutine_handle<TaskPromise<T>> h) noexcept {
                        return h.promise().waiter ? h.promise().waiter : std::noop_coroutine();
                    }
                    void await_resume() noexcept {}
                };
                return awaiter{};
            }
            auto unhandled_exception() -> void { exception = std::current_exception(); }
            template<typename U>
            auto await_transform(Task<U>&& childTask) -> Task<U>&& { return std::move(childTask); }
            template<typename U>
            auto await_transform(U&& task) -> U&& { return std::forward<U>(task); }
        };

        template<typename T = void>
        struct TaskPromise : public TaskPromiseBase<T>
        {
            auto get_return_object() -> Task<T> { return Task<T>::handle_type::from_promise(*this); }
            std::optional<T> value{};
            auto return_value(T val) -> void { value.emplace(std::move(val)); }
        };

        template<>
        struct TaskPromise<void> : public TaskPromiseBase<void>
        {
            auto get_return_object() -> Task<void> { return Task<void>::handle_type::from_promise(*this); }
            auto return_void() -> void {}
        };

        inline auto DetachedTaskPromise::get_return_object() -> DetachedTask 
        { 
            return { std::coroutine_handle<DetachedTaskPromise>::from_promise(*this) }; 
        }
    }

    // 4. Async Helpers
    template<typename T>
    auto runAsync(auto&& func) -> Task<T>
    {
        struct Awaiter
        {
            std::decay_t<decltype(func)> f;
            std::conditional_t<std::is_void_v<T>, bool, T> result{};

            bool await_ready() { return false; }
            void await_suspend(std::coroutine_handle<> h)
            {
                std::thread([this, h]() mutable {
                    try {
                        if constexpr (std::is_void_v<T>) f();
                        else result = f();
                    } catch (...) {}
                    h.resume();
                }).detach();
            }
            T await_resume() { if constexpr (!std::is_void_v<T>) return std::move(result); }
        };
        co_return co_await Awaiter{ std::forward<decltype(func)>(func) };
    }

    template<typename Rep, typename Period>
    auto sleep(std::chrono::duration<Rep, Period> duration) -> Task<void>
    {
        struct Awaiter
        {
            std::chrono::duration<Rep, Period> d;
            bool await_ready() { return d.count() <= 0; }
            void await_suspend(std::coroutine_handle<> h)
            {
                std::thread([this, h]() {
                    std::this_thread::sleep_for(d);
                    h.resume();
                }).detach();
            }
            void await_resume() {}
        };
        co_await Awaiter{ duration };
        co_return;
    }
}
