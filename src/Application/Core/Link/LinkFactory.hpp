#pragma once

#include "Common/Result.hpp"
#include "ILink.hpp"
#include <memory>
#include <string>

namespace core::link
{
    struct LinkConfig
    {
        std::string ip;
        uint16_t port{ 0 };
        std::string localNetId;
        std::string remoteNetId;
        bool inProcess{ false };
        std::string instanceName{ "default" };
    };

    auto create(Role role, Mode mode, Protocol proto, const LinkConfig& config)
      -> result::Result<std::unique_ptr<ILink>>;
}