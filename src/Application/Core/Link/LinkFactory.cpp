#include "LinkFactory.hpp"
#include "Raw/TcpServer.hpp"
#include "Symbolic/AdsClient.hpp"
#include "Symbolic/LocalAdsLink.hpp"
#include "Symbolic/OpcUaClient.hpp"

#include <system_error>

namespace core::link
{
    auto create(Role role, Mode mode, Protocol proto, const LinkConfig& config)
      -> result::Result<std::unique_ptr<ILink>>
    {
        if (mode == Mode::Raw) {
            if (role == Role::Server && proto == Protocol::Tcp) {
                return std::make_unique<raw::TcpServer>(config.port);
            }
        }

        if (mode == Mode::Symbolic && role == Role::Client) {
            if (proto == Protocol::Ads) {
                if (config.inProcess) {
                    return std::make_unique<symbolic::LocalAdsLink>(config.instanceName);
                }
                return std::make_unique<symbolic::AdsClient>(
                  config.remoteNetId, config.ip, config.port, config.localNetId);
            }
            if (proto == Protocol::OpcUa) {
                return std::make_unique<symbolic::OpcUaClient>(config.ip);
            }
        }

        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
}
