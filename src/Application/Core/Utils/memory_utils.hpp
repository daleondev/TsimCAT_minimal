#pragma once

#include <concepts>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>

namespace core::utils::memory
{
    template<typename T>
    concept Serializable = std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>;

    auto memcpy(Serializable auto& dest, const Serializable auto& src) -> bool
    {
        auto srcBytes{ std::as_bytes(std::span{ &src, 1 }) };
        auto destBytes{ std::as_writable_bytes(std::span{ &dest, 1 }) };
        if (srcBytes.size() != destBytes.size()) {
            return false;
        }
        std::ranges::copy(srcBytes, destBytes.begin());
        return true;
    }

    auto memcpy(Serializable auto& dest, const std::ranges::contiguous_range auto& src) -> bool
    {
        auto srcBytes{ std::as_bytes(std::span{ src }) };
        auto destBytes{ std::as_writable_bytes(std::span{ &dest, 1 }) };
        if (srcBytes.size() != destBytes.size()) {
            return false;
        }
        std::ranges::copy(srcBytes, destBytes.begin());
        return true;
    }

    auto memcpy(std::ranges::contiguous_range auto& dest, const Serializable auto& src) -> bool
    {
        auto srcBytes{ std::as_bytes(std::span{ &src, 1 }) };
        auto destBytes{ std::as_writable_bytes(std::span{ dest }) };
        if (srcBytes.size() != destBytes.size()) {
            return false;
        }
        std::ranges::copy(srcBytes, destBytes.begin());
        return true;
    }

    auto memcpy(std::ranges::contiguous_range auto& dest, const std::ranges::contiguous_range auto& src)
      -> bool
    {
        auto srcBytes{ std::as_bytes(std::span{ src }) };
        auto destBytes{ std::as_writable_bytes(std::span{ dest }) };
        if (srcBytes.size() != destBytes.size()) {
            return false;
        }
        std::ranges::copy(srcBytes, destBytes.begin());
        return true;
    }

    template<auto DeleteFn>
    struct Deleter
    {
        void operator()(auto* ptr) const
        {
            static_assert(std::invocable<decltype(DeleteFn), decltype(ptr)>,
                          "The provided function signature does not match the object type.");
            if (ptr) {
                DeleteFn(ptr);
            }
        }
    };
}