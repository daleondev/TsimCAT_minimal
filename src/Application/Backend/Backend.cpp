#include "Backend.h"

#include "Link/ILink.hpp"
#include "Link/LinkFactory.hpp"
#include "Link/Symbolic/ISymbolicLink.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace
{
    template<typename T>
    auto toQCoroTask(core::coro::Task<T>&& task) -> QCoro::Task<T>
    {
        co_return co_await std::move(task);
    }
}

namespace backend
{
    struct Backend::SharedAdsConfig
    {
        QString serverIp{ QStringLiteral("127.0.0.1") };
        QString serverNetId{ QStringLiteral("127.0.0.1.1.1") };
        QString localNetId{ QStringLiteral("127.0.0.1.1.20") };
        int adsPort{ 851 };
        QString rotaryActualPositionName;
        QString rotaryActualPositionType{ QStringLiteral("double") };
    };

    Backend::Backend(QObject* parent)
      : QObject(parent)
      , m_adsConfig(new AdsConfigBackend(this))
      , m_rotaryTable(new RotaryTableBackend(this))
    {
        launchTask(initializeSharedAdsLinkAsync());
    }

    Backend::~Backend() = default;

    auto Backend::adsConfig() const -> AdsConfigBackend* { return m_adsConfig; }

    auto Backend::rotaryTable() const -> RotaryTableBackend* { return m_rotaryTable; }

    QString Backend::welcomeMessage() const { return QStringLiteral("Hello from C++ Backend!"); }

    void Backend::launchTask(QCoro::Task<void>&& task)
    {
        auto guarded = std::move(task).then([]() {}, [](const std::exception&) {});
        QCoro::connect(std::move(guarded), this, []() {});
    }

    auto Backend::configFilePath() const -> QString
    {
        const QDir appDir(QCoreApplication::applicationDirPath());
        const QStringList candidateDirs{ appDir.filePath(QStringLiteral("config")),
                                         appDir.filePath(QStringLiteral("../../config")) };

        for (const auto& directory : candidateDirs) {
            const auto candidateFile = QDir(directory).filePath(QStringLiteral("ads_config.json"));
            if (QFileInfo::exists(candidateFile)) {
                return QDir::cleanPath(candidateFile);
            }
        }

        return QDir(candidateDirs.front()).filePath(QStringLiteral("ads_config.json"));
    }

    auto Backend::loadSharedAdsConfig() const -> SharedAdsConfig
    {
        SharedAdsConfig config;

        QFile file(configFilePath());
        if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
            return config;
        }

        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            return config;
        }

        const auto root = document.object();
        config.serverIp = root.value(QStringLiteral("serverIp")).toString(config.serverIp);
        config.serverNetId = root.value(QStringLiteral("serverNetId")).toString(config.serverNetId);
        config.localNetId = root.value(QStringLiteral("localNetId")).toString(config.localNetId);
        config.adsPort = root.value(QStringLiteral("adsPort")).toInt(config.adsPort);

        const auto adsVariables = root.value(QStringLiteral("adsVariables")).toObject();
        const auto rotaryTable = adsVariables.value(QStringLiteral("rotaryTable")).toObject();
        const auto actualPosition = rotaryTable.value(QStringLiteral("actualPosition")).toObject();
        config.rotaryActualPositionName = actualPosition.value(QStringLiteral("name")).toString();
        config.rotaryActualPositionType =
          actualPosition.value(QStringLiteral("type")).toString(config.rotaryActualPositionType);

        return config;
    }

    auto Backend::initializeSharedAdsLinkAsync() -> QCoro::Task<void>
    {
        const auto config = loadSharedAdsConfig();
        if (config.rotaryActualPositionName.trimmed().isEmpty()) {
            co_return;
        }

        if (config.rotaryActualPositionType.compare(QStringLiteral("double"), Qt::CaseInsensitive) != 0) {
            co_return;
        }

        core::link::LinkConfig linkConfig;
        linkConfig.ip = config.serverIp.toStdString();
        linkConfig.port = static_cast<uint16_t>(config.adsPort);
        linkConfig.localNetId = config.localNetId.toStdString();
        linkConfig.remoteNetId = config.serverNetId.toStdString();

        auto linkResult = core::link::create(
          core::link::Role::Client, core::link::Mode::Symbolic, core::link::Protocol::Ads, linkConfig);
        if (!linkResult) {
            co_return;
        }

        auto link = std::move(linkResult).value();
        auto* client = link->asClient();
        auto* symbolicLink = link->asSymbolic();
        if (!client || !symbolicLink) {
            co_return;
        }

        auto connectResult = co_await toQCoroTask(client->connect());
        if (!connectResult) {
            co_return;
        }

        m_sharedAdsSymbolicLink = symbolicLink;
        m_sharedAdsLink = std::move(link);
        m_rotaryTable->subscribeActualPosition(m_sharedAdsSymbolicLink, config.rotaryActualPositionName);
    }
}
