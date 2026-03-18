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

#include <algorithm>

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
    namespace
    {
        struct VariableConfig
        {
            QString name;
            QString type;

            auto isConfigured() const -> bool { return !name.trimmed().isEmpty(); }
            auto hasType(QStringView expectedType) const -> bool
            {
                return type.trimmed().compare(expectedType, Qt::CaseInsensitive) == 0;
            }

            auto isFloatLike() const -> bool
            {
                return hasType(QStringLiteral("float")) || hasType(QStringLiteral("double"));
            }

            auto isBoolean() const -> bool { return hasType(QStringLiteral("bool")); }

            auto isIntegerLike() const -> bool
            {
                return hasType(QStringLiteral("int16")) || hasType(QStringLiteral("uint16")) ||
                       hasType(QStringLiteral("int32")) || hasType(QStringLiteral("uint32")) ||
                       hasType(QStringLiteral("int")) || hasType(QStringLiteral("uint"));
            }
        };

        auto toJobIdType(const VariableConfig& config) -> RobotBackend::JobIdType
        {
            if (config.hasType(QStringLiteral("int16"))) {
                return RobotBackend::JobIdType::Int16;
            }
            if (config.hasType(QStringLiteral("uint16")) || config.hasType(QStringLiteral("uint"))) {
                return RobotBackend::JobIdType::UInt16;
            }
            if (config.hasType(QStringLiteral("uint32"))) {
                return RobotBackend::JobIdType::UInt32;
            }
            return RobotBackend::JobIdType::Int32;
        }

        auto readVariableConfig(const QJsonObject& object, QStringView key, QStringView defaultType)
          -> VariableConfig
        {
            const auto value = object.value(key.toString()).toObject();
            return VariableConfig{ .name = value.value(QStringLiteral("name")).toString(),
                                   .type =
                                     value.value(QStringLiteral("type")).toString(defaultType.toString()) };
        }
    }

    struct Backend::SharedAdsConfig
    {
        QString serverIp{ QStringLiteral("127.0.0.1") };
        QString serverNetId{ QStringLiteral("127.0.0.1.1.1") };
        QString localNetId{ QStringLiteral("127.0.0.1.1.20") };
        int adsPort{ 851 };
        VariableConfig rotaryActualPosition;
        VariableConfig rotarySensor0;
        VariableConfig rotarySensor180;
        VariableConfig conveyorRun;
        std::array<VariableConfig, 4> conveyorSensors;
        VariableConfig robotJobId;
        VariableConfig robotActualJobId;
        VariableConfig robotGripperSensor;
        std::array<VariableConfig, 4> robotAreaFreePlc;
        std::array<VariableConfig, 4> robotAreaFreeRobot;
    };

    Backend::Backend(QObject* parent)
      : QObject(parent)
      , m_adsConfig(new AdsConfigBackend(this))
      , m_conveyor(new ConveyorBackend(this))
      , m_robot(new RobotBackend(this))
      , m_rotaryTable(new RotaryTableBackend(this))
    {
        launchTask(initializeSharedAdsLinkAsync());
    }

    Backend::~Backend()
    {
        if (m_rotaryTable) {
            m_rotaryTable->detachSymbolicLink();
        }
        if (m_conveyor) {
            m_conveyor->detachSymbolicLink();
        }
        if (m_robot) {
            m_robot->detachSymbolicLink();
        }
    }

    auto Backend::adsConfig() const -> AdsConfigBackend* { return m_adsConfig; }

    auto Backend::conveyor() const -> ConveyorBackend* { return m_conveyor; }

    auto Backend::robot() const -> RobotBackend* { return m_robot; }

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
        const auto conveyor = adsVariables.value(QStringLiteral("conveyor")).toObject();
        const auto robot = adsVariables.value(QStringLiteral("robot")).toObject();

        config.rotaryActualPosition =
          readVariableConfig(rotaryTable, QStringLiteral("actualPosition"), QStringLiteral("double"));
        config.rotarySensor0 =
          readVariableConfig(rotaryTable, QStringLiteral("sensor0"), QStringLiteral("bool"));
        config.rotarySensor180 =
          readVariableConfig(rotaryTable, QStringLiteral("sensor180"), QStringLiteral("bool"));
        config.conveyorRun = readVariableConfig(conveyor, QStringLiteral("run"), QStringLiteral("bool"));
        config.conveyorSensors = {
            readVariableConfig(conveyor, QStringLiteral("sensor1"), QStringLiteral("bool")),
            readVariableConfig(conveyor, QStringLiteral("sensor2"), QStringLiteral("bool")),
            readVariableConfig(conveyor, QStringLiteral("sensor3"), QStringLiteral("bool")),
            readVariableConfig(conveyor, QStringLiteral("sensor4"), QStringLiteral("bool"))
        };
        config.robotJobId = readVariableConfig(robot, QStringLiteral("jobId"), QStringLiteral("int32"));
        config.robotActualJobId =
          readVariableConfig(robot, QStringLiteral("actualJobId"), QStringLiteral("int32"));
        config.robotGripperSensor =
          readVariableConfig(robot, QStringLiteral("gripperSensor"), QStringLiteral("bool"));
        config.robotAreaFreePlc = {
            readVariableConfig(robot, QStringLiteral("areaFreePLC1"), QStringLiteral("bool")),
            readVariableConfig(robot, QStringLiteral("areaFreePLC2"), QStringLiteral("bool")),
            readVariableConfig(robot, QStringLiteral("areaFreePLC3"), QStringLiteral("bool")),
            readVariableConfig(robot, QStringLiteral("areaFreePLC4"), QStringLiteral("bool"))
        };
        config.robotAreaFreeRobot = {
            readVariableConfig(robot, QStringLiteral("areaFreeRobot1"), QStringLiteral("bool")),
            readVariableConfig(robot, QStringLiteral("areaFreeRobot2"), QStringLiteral("bool")),
            readVariableConfig(robot, QStringLiteral("areaFreeRobot3"), QStringLiteral("bool")),
            readVariableConfig(robot, QStringLiteral("areaFreeRobot4"), QStringLiteral("bool"))
        };

        return config;
    }

    auto Backend::initializeSharedAdsLinkAsync() -> QCoro::Task<void>
    {
        const auto config = loadSharedAdsConfig();

        const auto hasConfiguredVariables =
          config.rotaryActualPosition.isConfigured() || config.rotarySensor0.isConfigured() ||
          config.rotarySensor180.isConfigured() || config.conveyorRun.isConfigured() ||
          config.robotJobId.isConfigured() || config.robotActualJobId.isConfigured() ||
          config.robotGripperSensor.isConfigured() ||
          std::ranges::any_of(config.conveyorSensors,
                              [](const auto& variable) { return variable.isConfigured(); }) ||
          std::ranges::any_of(config.robotAreaFreePlc,
                              [](const auto& variable) { return variable.isConfigured(); }) ||
          std::ranges::any_of(config.robotAreaFreeRobot,
                              [](const auto& variable) { return variable.isConfigured(); });
        if (!hasConfiguredVariables) {
            co_return;
        }

        if (config.rotaryActualPosition.isConfigured() && !config.rotaryActualPosition.isFloatLike()) {
            co_return;
        }

        if (config.conveyorRun.isConfigured() && !config.conveyorRun.hasType(QStringLiteral("bool"))) {
            co_return;
        }

        if (config.robotJobId.isConfigured() && !config.robotJobId.isIntegerLike()) {
            co_return;
        }

        if (config.robotActualJobId.isConfigured() && !config.robotActualJobId.isIntegerLike()) {
            co_return;
        }

        if (config.robotGripperSensor.isConfigured() && !config.robotGripperSensor.isBoolean()) {
            co_return;
        }

        if (std::ranges::any_of(config.robotAreaFreePlc, [](const auto& variable) {
            return variable.isConfigured() && !variable.isBoolean();
        })) {
            co_return;
        }

        if (std::ranges::any_of(config.robotAreaFreeRobot, [](const auto& variable) {
            return variable.isConfigured() && !variable.isBoolean();
        })) {
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

        m_rotaryTable->configureSensorVariables(
          m_sharedAdsSymbolicLink, config.rotarySensor0.name, config.rotarySensor180.name);
        m_conveyor->configureSensorVariables(m_sharedAdsSymbolicLink,
                                             { config.conveyorSensors[0].name,
                                               config.conveyorSensors[1].name,
                                               config.conveyorSensors[2].name,
                                               config.conveyorSensors[3].name });
        m_robot->setConveyorBackend(m_conveyor);
        m_robot->setRotaryTableBackend(m_rotaryTable);
        m_robot->configureAds(m_sharedAdsSymbolicLink,
                                                            RobotBackend::AdsConfig{ .jobIdVariable = config.robotJobId.name,
                                                                                                             .actualJobIdVariable = config.robotActualJobId.name,
                                                                                                             .gripperSensorVariable =
                                                                                                                 config.robotGripperSensor.name,
                                                                                                             .jobIdType = toJobIdType(config.robotJobId),
                                                                                                             .actualJobIdType =
                                                                                                                 toJobIdType(config.robotActualJobId),
                                                                                                             .areaFreePlcVariables = {
                                                                                                                     config.robotAreaFreePlc[0].name,
                                                                                                                     config.robotAreaFreePlc[1].name,
                                                                                                                     config.robotAreaFreePlc[2].name,
                                                                                                                     config.robotAreaFreePlc[3].name,
                                                                                                             },
                                                                                                             .areaFreeRobotVariables = {
                                                                                                                     config.robotAreaFreeRobot[0].name,
                                                                                                                     config.robotAreaFreeRobot[1].name,
                                                                                                                     config.robotAreaFreeRobot[2].name,
                                                                                                                     config.robotAreaFreeRobot[3].name,
                                                                                                             } });

        if (config.rotaryActualPosition.isConfigured()) {
            const auto actualPositionType = config.rotaryActualPosition.hasType(QStringLiteral("float"))
                                              ? RotaryTableBackend::ActualPositionType::Float
                                              : RotaryTableBackend::ActualPositionType::Double;
            m_rotaryTable->subscribeActualPosition(
              m_sharedAdsSymbolicLink, config.rotaryActualPosition.name, actualPositionType);
        }

        if (config.conveyorRun.isConfigured()) {
            m_conveyor->subscribeRun(m_sharedAdsSymbolicLink, config.conveyorRun.name);
        }

        co_return;
    }
}
