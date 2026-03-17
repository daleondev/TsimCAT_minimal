#pragma once

#include <expected>
#include <system_error>

namespace core::result
{
    template<typename T>
    using Result = std::expected<T, std::error_code>;

    inline constexpr auto success() -> Result<void> { return {}; }
}