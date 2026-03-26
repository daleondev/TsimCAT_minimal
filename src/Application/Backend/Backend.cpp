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
#include <QMetaObject>

#include <algorithm>
#include <iostream>

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
                return hasType(QStringLiteral("int8")) || hasType(QStringLiteral("uint8")) ||
                       hasType(QStringLiteral("int16")) || hasType(QStringLiteral("uint16")) ||
                       hasType(QStringLiteral("int32")) || hasType(QStringLiteral("uint32")) ||
                       hasType(QStringLiteral("int")) || hasType(QStringLiteral("uint"));
            }
        };

        auto toJobIdType(const VariableConfig& config) -> RobotBackend::JobIdType
        {
            if (config.hasType(QStringLiteral("int8"))) {
                return RobotBackend::JobIdType::Int8;
            }
            if (config.hasType(QStringLiteral("uint8"))) {
                return RobotBackend::JobIdType::UInt8;
            }
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
        VariableConfig conveyorDamperMoveUp;
        VariableConfig conveyorDamperMoveDown;
        VariableConfig conveyorDamperUpSensor;
        VariableConfig conveyorDamperDownSensor;
        VariableConfig robotJobId;
        VariableConfig robotActualJobId;
        VariableConfig robotGripperSensor;
        VariableConfig simulationReset;
    };

    Backend::Backend(QObject* parent)
      : QObject(parent)
      , m_adsConfig(new AdsConfigBackend(this))
      , m_conveyor(new ConveyorBackend(this))
      , m_robot(new RobotBackend(this))
      , m_rotaryTable(new RotaryTableBackend(this))
      , m_simulationResetPollTimer(new QTimer(this))
    {
        m_simulationResetPollTimer->setInterval(100);
        connect(m_simulationResetPollTimer, &QTimer::timeout, this, &Backend::startSimulationResetPoll);
        launchTask(initializeSharedAdsLinkAsync());
    }

    Backend::~Backend()
    {
        resetSimulationResetControl();

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

    void Backend::resetSimulationState()
    {
        if (m_robot) {
            m_robot->resetSimulationState();
        }

        if (m_conveyor) {
            m_conveyor->resetSimulationState();
        }

        if (m_rotaryTable) {
            m_rotaryTable->resetSimulationState();
        }
    }

    void Backend::resyncAds()
    {
        resetSimulationState();
        launchTask(initializeSharedAdsLinkAsync());
    }

    void Backend::launchTask(QCoro::Task<void>&& task)
    {
        auto guarded = std::move(task).then([]() {}, [](const std::exception&) {});
        QCoro::connect(std::move(guarded), this, []() {});
    }

    void Backend::setSimulationEnabled(bool enabled)
    {
        if (m_robot) {
            m_robot->setSimulationEnabled(enabled);
        }

        if (m_conveyor) {
            m_conveyor->setSimulationEnabled(enabled);
        }

        if (m_rotaryTable) {
            m_rotaryTable->setSimulationEnabled(enabled);
        }
    }

    void Backend::resetSimulationResetControl()
    {
        if (m_simulationResetPollTimer) {
            m_simulationResetPollTimer->stop();
        }

        m_simulationResetVariableName.clear();
        m_simulationResetPollInFlight = false;
        ++m_simulationResetGeneration;
        m_lastSimulationResetCommand = false;
    }

    void Backend::startSimulationResetPoll()
    {
        if (!m_sharedAdsSymbolicLink || m_simulationResetPollInFlight ||
            m_simulationResetVariableName.trimmed().isEmpty()) {
            return;
        }

        m_simulationResetPollInFlight = true;
        launchTask(pollSimulationResetAsync(m_simulationResetVariableName, m_simulationResetGeneration));
    }

    void Backend::applySimulationResetCommand(bool resetCommand)
    {
        std::cout << "[reset-trace] reset_command current=" << resetCommand
                  << " previous=" << m_lastSimulationResetCommand;
        if (m_rotaryTable) {
            std::cout << " rotaryAngle=" << m_rotaryTable->angleDegrees()
                      << " sensor0=" << m_rotaryTable->sensor0Active()
                      << " sensor180=" << m_rotaryTable->sensor180Active()
                      << " part0=" << m_rotaryTable->part0Present()
                      << " part180=" << m_rotaryTable->part180Present();
        }
        std::cout << std::endl;

        if (resetCommand && !m_lastSimulationResetCommand) {
            resetSimulationState();
            setSimulationEnabled(false);
        }
        else if (!resetCommand && m_lastSimulationResetCommand) {
            launchTask(refreshPlcSignalsOnceAsync(true));
        }

        m_lastSimulationResetCommand = resetCommand;
    }

    auto Backend::refreshPlcSignalsOnceAsync(bool enableSimulationBeforeRobotPoll) -> QCoro::Task<void>
    {
        std::cout << "[reset-trace] refresh_begin enableBeforeRobot=" << enableSimulationBeforeRobotPoll;
        if (m_rotaryTable) {
            std::cout << " rotaryAngle=" << m_rotaryTable->angleDegrees()
                      << " sensor0=" << m_rotaryTable->sensor0Active()
                      << " sensor180=" << m_rotaryTable->sensor180Active()
                      << " part0=" << m_rotaryTable->part0Present()
                      << " part180=" << m_rotaryTable->part180Present();
        }
        std::cout << std::endl;

        if (m_conveyor) {
            co_await m_conveyor->pollAdsStateOnce();
        }

        if (m_rotaryTable) {
            co_await m_rotaryTable->pollAdsStateOnce();
        }

        std::cout << "[reset-trace] refresh_after_rotary";
        if (m_rotaryTable) {
            std::cout << " rotaryAngle=" << m_rotaryTable->angleDegrees()
                      << " sensor0=" << m_rotaryTable->sensor0Active()
                      << " sensor180=" << m_rotaryTable->sensor180Active()
                      << " part0=" << m_rotaryTable->part0Present()
                      << " part180=" << m_rotaryTable->part180Present();
        }
        std::cout << std::endl;

        if (enableSimulationBeforeRobotPoll) {
            setSimulationEnabled(true);
            std::cout << "[reset-trace] refresh_after_enable";
            if (m_rotaryTable) {
                std::cout << " rotaryAngle=" << m_rotaryTable->angleDegrees()
                          << " sensor0=" << m_rotaryTable->sensor0Active()
                          << " sensor180=" << m_rotaryTable->sensor180Active()
                          << " part0=" << m_rotaryTable->part0Present()
                          << " part180=" << m_rotaryTable->part180Present();
            }
            std::cout << std::endl;
        }

        if (m_robot) {
            co_await m_robot->pollAdsStateOnce();
        }

        std::cout << "[reset-trace] refresh_complete";
        if (m_rotaryTable) {
            std::cout << " rotaryAngle=" << m_rotaryTable->angleDegrees()
                      << " sensor0=" << m_rotaryTable->sensor0Active()
                      << " sensor180=" << m_rotaryTable->sensor180Active()
                      << " part0=" << m_rotaryTable->part0Present()
                      << " part180=" << m_rotaryTable->part180Present();
        }
        std::cout << std::endl;

        co_return;
    }

    auto Backend::pollSimulationResetAsync(QString variableName, size_t generation) -> QCoro::Task<void>
    {
        if (!m_sharedAdsSymbolicLink || variableName.trimmed().isEmpty()) {
            co_return;
        }

        auto currentValue =
          co_await toQCoroTask(m_sharedAdsSymbolicLink->read<bool>(variableName.toStdString()));

        QMetaObject::invokeMethod(this, [this, generation, currentValue = std::move(currentValue)]() mutable {
            if (generation != m_simulationResetGeneration) {
                return;
            }

            m_simulationResetPollInFlight = false;

            if (!currentValue) {
                return;
            }

            applySimulationResetCommand(currentValue.value());
        }, Qt::QueuedConnection);

        co_return;
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
        const auto simulation = adsVariables.value(QStringLiteral("simulation")).toObject();

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
        config.conveyorDamperMoveUp =
          readVariableConfig(conveyor, QStringLiteral("damperMoveUp"), QStringLiteral("bool"));
        config.conveyorDamperMoveDown =
          readVariableConfig(conveyor, QStringLiteral("damperMoveDown"), QStringLiteral("bool"));
        config.conveyorDamperUpSensor =
          readVariableConfig(conveyor, QStringLiteral("damperUpSensor"), QStringLiteral("bool"));
        config.conveyorDamperDownSensor =
          readVariableConfig(conveyor, QStringLiteral("damperDownSensor"), QStringLiteral("bool"));
        config.robotJobId = readVariableConfig(robot, QStringLiteral("jobId"), QStringLiteral("int32"));
        config.robotActualJobId =
          readVariableConfig(robot, QStringLiteral("actualJobId"), QStringLiteral("int32"));
        config.robotGripperSensor =
          readVariableConfig(robot, QStringLiteral("gripperSensor"), QStringLiteral("bool"));
        config.simulationReset =
          readVariableConfig(simulation, QStringLiteral("reset"), QStringLiteral("bool"));

        return config;
    }

    auto Backend::initializeSharedAdsLinkAsync() -> QCoro::Task<void>
    {
        const auto config = loadSharedAdsConfig();

        if (m_rotaryTable) {
            m_rotaryTable->detachSymbolicLink();
        }
        if (m_conveyor) {
            m_conveyor->detachSymbolicLink();
        }
        if (m_robot) {
            m_robot->detachSymbolicLink();
        }

        resetSimulationResetControl();

        if (m_sharedAdsLink) {
            if (auto* client = m_sharedAdsLink->asClient()) {
                (void)co_await toQCoroTask(client->disconnect());
            }

            m_sharedAdsSymbolicLink = nullptr;
            m_sharedAdsLink.reset();
        }

        const auto hasConfiguredVariables =
          config.rotaryActualPosition.isConfigured() || config.rotarySensor0.isConfigured() ||
          config.rotarySensor180.isConfigured() || config.conveyorRun.isConfigured() ||
          config.conveyorDamperMoveUp.isConfigured() || config.conveyorDamperMoveDown.isConfigured() ||
          config.conveyorDamperUpSensor.isConfigured() || config.conveyorDamperDownSensor.isConfigured() ||
          config.robotJobId.isConfigured() || config.robotActualJobId.isConfigured() ||
          config.robotGripperSensor.isConfigured() || config.simulationReset.isConfigured() ||
          std::ranges::any_of(config.conveyorSensors,
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

        if ((config.conveyorDamperMoveUp.isConfigured() && !config.conveyorDamperMoveUp.isBoolean()) ||
            (config.conveyorDamperMoveDown.isConfigured() && !config.conveyorDamperMoveDown.isBoolean()) ||
            (config.conveyorDamperUpSensor.isConfigured() && !config.conveyorDamperUpSensor.isBoolean()) ||
            (config.conveyorDamperDownSensor.isConfigured() &&
             !config.conveyorDamperDownSensor.isBoolean())) {
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

        if (config.simulationReset.isConfigured() && !config.simulationReset.isBoolean()) {
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
        m_conveyor->configureDamperVariables(m_sharedAdsSymbolicLink,
                                             config.conveyorDamperMoveUp.name,
                                             config.conveyorDamperMoveDown.name,
                                             config.conveyorDamperUpSensor.name,
                                             config.conveyorDamperDownSensor.name);
        m_robot->setConveyorBackend(m_conveyor);
        m_robot->setRotaryTableBackend(m_rotaryTable);
        m_robot->configureAds(
          m_sharedAdsSymbolicLink,
          RobotBackend::AdsConfig{ .jobIdVariable = config.robotJobId.name,
                                   .actualJobIdVariable = config.robotActualJobId.name,
                                   .gripperSensorVariable = config.robotGripperSensor.name,
                                   .jobIdType = toJobIdType(config.robotJobId),
                                   .actualJobIdType = toJobIdType(config.robotActualJobId) });

        resetSimulationState();

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

        if (config.simulationReset.isConfigured()) {
            m_simulationResetVariableName = config.simulationReset.name;
            if (m_simulationResetPollTimer) {
                m_simulationResetPollTimer->start();
            }
            startSimulationResetPoll();
        }

        setSimulationEnabled(true);
        co_await refreshPlcSignalsOnceAsync(false);

        co_return;
    }
}
