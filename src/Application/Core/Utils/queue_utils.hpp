#pragma once

#include <concepts>
#include <mutex>
#include <optional>

namespace core::utils::queue
{
    namespace detail
    {
        template<typename T>
        concept Queue = requires(T t, typename T::value_type v) {
            typename T::value_type;

            requires(requires {
                { t.empty() } -> std::same_as<bool>;
            } || requires {
                { t.isEmpty() } -> std::same_as<bool>;
            });

            requires(requires {
                { t.front() } -> std::same_as<typename T::value_type&>;
            } || requires {
                { t.head() } -> std::same_as<typename T::value_type&>;
            });

            requires(requires {
                { t.push(v) } -> std::same_as<void>;
            } || requires {
                { t.push_back(v) } -> std::same_as<void>;
            } || requires {
                { t.enqueue(v) } -> std::same_as<void>;
            });

            requires(requires {
                { t.pop() } -> std::same_as<void>;
            } || requires {
                { t.pop_front() } -> std::same_as<void>;
            } || requires {
                { t.dequeue() } -> std::same_as<typename T::value_type>;
            });
        };

        template<typename T>
        concept Lockable = requires(T t) {
            { t.lock() };
            { t.unlock() };
        };

        struct NotLocked
        {
            auto lock() -> void {}
            auto unlock() -> void {}
        };
    }

    template<detail::Lockable L = detail::NotLocked>
    auto pop(detail::Queue auto& queue, L&& lockable = detail::NotLocked{})
      -> std::optional<typename std::remove_reference_t<decltype(queue)>::value_type>
    {
        std::scoped_lock lock{ lockable };

        if (queue.empty()) {
            return std::nullopt;
        }

        if constexpr (requires { queue.dequeue(); }) {
            return queue.dequeue();
        }
        else {
            auto val{ std::move(queue.front()) };
            if constexpr (requires { queue.pop(); }) {
                queue.pop();
            }
            else {
                queue.pop_front();
            }
            return val;
        }
    }

    template<detail::Lockable L = detail::NotLocked>
    auto push(detail::Queue auto& queue, auto value, L&& lockable = detail::NotLocked{}) -> void
    {
        std::scoped_lock lock{ lockable };

        if constexpr (requires { queue.push(value); }) {
            queue.push(std::move(value));
        }
        else if constexpr (requires { queue.push_back(value); }) {
            queue.push_back(std::move(value));
        }
        else {
            queue.enqueue(std::move(value));
        }
    }
}