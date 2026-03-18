#include "ConveyorBackend.h"

#include "Link/Symbolic/ISymbolicLink.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
    constexpr std::array<double, 4> kSensorPositions = { 100.0, 500.0, 750.0, 1150.0 };
    constexpr double kPlacedPartStartPosition = 100.0;
    constexpr double kConveyorExitPosition = 1250.0;
    constexpr double kTakeAtExitThreshold = 1120.0;
    constexpr double kPartHalfLength = 70.0;
    constexpr double kConveyorSpeedPerSecond = 220.0;
    constexpr double kDamperTravelPerSecond = 0.7;
    constexpr double kDamperSensorTolerance = 0.001;

    template<typename T>
    auto toQCoroTask(core::coro::Task<T>&& task) -> QCoro::Task<T>
    {
        co_return co_await std::move(task);
    }
}

namespace backend
{
    ConveyorBackend::ConveyorBackend(QObject* parent)
      : QObject(parent)
      , m_motionTimer(new QTimer(this))
    {
        m_motionTimer->setInterval(16);
        connect(m_motionTimer, &QTimer::timeout, this, &ConveyorBackend::onSimulationTick);
        m_motionTimer->start();
        m_stepClock.start();
    }

    ConveyorBackend::~ConveyorBackend() { detachSymbolicLink(); }

    auto ConveyorBackend::partPresent() const -> bool { return m_partPresent; }

    auto ConveyorBackend::partPosition() const -> double { return m_partPosition; }

    auto ConveyorBackend::damperOpen() const -> bool { return m_damperPosition > kDamperSensorTolerance; }

    auto ConveyorBackend::damperPosition() const -> double { return m_damperPosition; }

    auto ConveyorBackend::damperMovingUpCommand() const -> bool { return m_damperMoveUpCommand; }

    auto ConveyorBackend::damperMovingDownCommand() const -> bool { return m_damperMoveDownCommand; }

    auto ConveyorBackend::damperUpSensorActive() const -> bool { return m_damperUpSensorActive; }

    auto ConveyorBackend::damperDownSensorActive() const -> bool { return m_damperDownSensorActive; }

    auto ConveyorBackend::running() const -> bool { return m_running; }

    auto ConveyorBackend::sensors() const -> QVariantList
    {
        QVariantList values;
        values.reserve(static_cast<qsizetype>(m_sensors.size()));
        for (const auto sensor : m_sensors) {
            values.push_back(sensor);
        }
        return values;
    }

    void ConveyorBackend::detachSymbolicLink()
    {
        unsubscribe(m_runSubscription);
        unsubscribe(m_damperMoveUpSubscription);
        unsubscribe(m_damperMoveDownSubscription);
        m_symbolicLink = nullptr;
    }

    void ConveyorBackend::subscribeRun(core::link::ISymbolicLink* symbolicLink, const QString& variableName)
    {
        if (symbolicLink) {
            m_symbolicLink = symbolicLink;
        }

        unsubscribe(m_runSubscription);

        if (!m_symbolicLink || variableName.trimmed().isEmpty()) {
            return;
        }

        launchTask(subscribeRunAsync(variableName));
    }

    void ConveyorBackend::configureSensorVariables(core::link::ISymbolicLink* symbolicLink,
                                                   const std::array<QString, 4>& variableNames)
    {
        if (symbolicLink) {
            m_symbolicLink = symbolicLink;
        }

        m_sensorVariableNames = variableNames;
        updateSensorsFromPart();
    }

    void ConveyorBackend::configureDamperVariables(core::link::ISymbolicLink* symbolicLink,
                                                   const QString& moveUpVariable,
                                                   const QString& moveDownVariable,
                                                   const QString& upSensorVariable,
                                                   const QString& downSensorVariable)
    {
        if (symbolicLink) {
            m_symbolicLink = symbolicLink;
        }

        unsubscribe(m_damperMoveUpSubscription);
        unsubscribe(m_damperMoveDownSubscription);

        m_damperUpSensorVariableName = upSensorVariable;
        m_damperDownSensorVariableName = downSensorVariable;
        updateDamperSensors();

        if (!m_symbolicLink) {
            return;
        }

        if (!moveUpVariable.trimmed().isEmpty()) {
            launchTask(subscribeDamperMoveUpAsync(moveUpVariable));
        }

        if (!moveDownVariable.trimmed().isEmpty()) {
            launchTask(subscribeDamperMoveDownAsync(moveDownVariable));
        }
    }

    auto ConveyorBackend::tryPlacePartFromRobot() -> bool
    {
        if (m_partPresent) {
            return false;
        }

        setPartPositionInternal(kPlacedPartStartPosition);
        setPartPresentInternal(true);
        updateSensorsFromPart();
        return true;
    }

    auto ConveyorBackend::tryTakePartAtExit() -> bool
    {
        if (!m_partPresent || m_partPosition < kTakeAtExitThreshold) {
            return false;
        }

        setPartPresentInternal(false);
        updateSensorsFromPart();
        return true;
    }

    void ConveyorBackend::setSensorActive(int index, bool active) { setSensorActiveInternal(index, active); }

    void ConveyorBackend::launchTask(QCoro::Task<void>&& task)
    {
        auto guarded = std::move(task).then([]() {}, [](const std::exception&) {});
        QCoro::connect(std::move(guarded), this, []() {});
    }

    void ConveyorBackend::unsubscribe(core::link::Subscription<bool>& subscription)
    {
        if (m_symbolicLink && subscription.isValid()) {
            m_symbolicLink->unsubscribeRawSync(subscription.id());
        }

        subscription = {};
    }

    void ConveyorBackend::onSimulationTick()
    {
        const auto elapsedMs = static_cast<double>(m_stepClock.restart());
        const auto deltaSeconds = std::max(0.001, elapsedMs / 1000.0);

        if (m_running && m_partPresent) {
            setPartPositionInternal(m_partPosition + deltaSeconds * kConveyorSpeedPerSecond);

            if (m_partPosition > kConveyorExitPosition) {
                setPartPresentInternal(false);
                updateSensorsFromPart();
            }
            else {
                updateSensorsFromPart();
            }
        }

        if (m_damperMoveUpCommand != m_damperMoveDownCommand) {
            const auto direction = m_damperMoveUpCommand ? 1.0 : -1.0;
            setDamperPositionInternal(m_damperPosition + direction * deltaSeconds * kDamperTravelPerSecond);
        }
    }

    void ConveyorBackend::setRunning(bool value)
    {
        if (m_running == value) {
            return;
        }

        m_running = value;
        emit runningChanged();
    }

    void ConveyorBackend::setPartPresentInternal(bool value)
    {
        if (m_partPresent == value) {
            return;
        }

        m_partPresent = value;
        emit partPresentChanged();

        if (!m_partPresent) {
            setPartPositionInternal(0.0);
        }
    }

    void ConveyorBackend::setPartPositionInternal(double value)
    {
        const auto nextValue = std::max(0.0, value);
        if (qFuzzyCompare(m_partPosition, nextValue)) {
            return;
        }

        m_partPosition = nextValue;
        emit partPositionChanged();
    }

    void ConveyorBackend::setDamperMoveUpCommand(bool value)
    {
        if (m_damperMoveUpCommand == value) {
            return;
        }

        m_damperMoveUpCommand = value;
        emit damperCommandsChanged();
    }

    void ConveyorBackend::setDamperMoveDownCommand(bool value)
    {
        if (m_damperMoveDownCommand == value) {
            return;
        }

        m_damperMoveDownCommand = value;
        emit damperCommandsChanged();
    }

    void ConveyorBackend::setDamperPositionInternal(double value)
    {
        const auto nextValue = std::clamp(value, 0.0, 1.0);
        if (qFuzzyCompare(m_damperPosition, nextValue)) {
            return;
        }

        const auto wasOpen = damperOpen();
        m_damperPosition = nextValue;
        emit damperPositionChanged();

        if (wasOpen != damperOpen()) {
            emit damperOpenChanged();
        }

        updateDamperSensors();
    }

    void ConveyorBackend::setSensorActiveInternal(int index, bool active)
    {
        if (index < 0 || index >= static_cast<int>(m_sensors.size())) {
            return;
        }

        if (m_sensors[static_cast<size_t>(index)] == active) {
            return;
        }

        m_sensors[static_cast<size_t>(index)] = active;

        emit sensorsChanged();

        const auto& variableName = m_sensorVariableNames[static_cast<size_t>(index)];
        if (m_symbolicLink && !variableName.trimmed().isEmpty()) {
            launchTask(writeBoolAsync(variableName, active));
        }
    }

    void ConveyorBackend::updateSensorsFromPart()
    {
        for (size_t index = 0; index < m_sensors.size(); ++index) {
            const auto sensorActive =
              m_partPresent && std::abs(m_partPosition - kSensorPositions[index]) <= kPartHalfLength;
            setSensorActiveInternal(static_cast<int>(index), sensorActive);
        }
    }

    void ConveyorBackend::updateDamperSensors()
    {
        const auto nextUpSensor = m_damperPosition >= 1.0 - kDamperSensorTolerance;
        const auto nextDownSensor = m_damperPosition <= kDamperSensorTolerance;

        auto sensorStateChanged = false;
        if (m_damperUpSensorActive != nextUpSensor) {
            m_damperUpSensorActive = nextUpSensor;
            sensorStateChanged = true;
            if (m_symbolicLink && !m_damperUpSensorVariableName.trimmed().isEmpty()) {
                launchTask(writeBoolAsync(m_damperUpSensorVariableName, m_damperUpSensorActive));
            }
        }

        if (m_damperDownSensorActive != nextDownSensor) {
            m_damperDownSensorActive = nextDownSensor;
            sensorStateChanged = true;
            if (m_symbolicLink && !m_damperDownSensorVariableName.trimmed().isEmpty()) {
                launchTask(writeBoolAsync(m_damperDownSensorVariableName, m_damperDownSensorActive));
            }
        }

        if (sensorStateChanged) {
            emit damperSensorsChanged();
        }
    }

    auto ConveyorBackend::subscribeRunAsync(QString variableName) -> QCoro::Task<void>
    {
        if (!m_symbolicLink || variableName.trimmed().isEmpty()) {
            co_return;
        }

        auto subscriptionResult =
          co_await toQCoroTask(m_symbolicLink->subscribe<bool>(variableName.toStdString()));
        if (!subscriptionResult) {
            co_return;
        }

        m_runSubscription = std::move(subscriptionResult).value();
        launchTask(consumeRunAsync());
    }

    auto ConveyorBackend::subscribeDamperMoveUpAsync(QString variableName) -> QCoro::Task<void>
    {
        if (!m_symbolicLink || variableName.trimmed().isEmpty()) {
            co_return;
        }

        auto subscriptionResult =
          co_await toQCoroTask(m_symbolicLink->subscribe<bool>(variableName.toStdString()));
        if (!subscriptionResult) {
            co_return;
        }

        m_damperMoveUpSubscription = std::move(subscriptionResult).value();
        launchTask(consumeDamperMoveUpAsync());
    }

    auto ConveyorBackend::subscribeDamperMoveDownAsync(QString variableName) -> QCoro::Task<void>
    {
        if (!m_symbolicLink || variableName.trimmed().isEmpty()) {
            co_return;
        }

        auto subscriptionResult =
          co_await toQCoroTask(m_symbolicLink->subscribe<bool>(variableName.toStdString()));
        if (!subscriptionResult) {
            co_return;
        }

        m_damperMoveDownSubscription = std::move(subscriptionResult).value();
        launchTask(consumeDamperMoveDownAsync());
    }

    auto ConveyorBackend::consumeRunAsync() -> QCoro::Task<void>
    {
        while (m_runSubscription.isValid()) {
            auto nextValue = co_await toQCoroTask(m_runSubscription.stream.next());
            if (!nextValue.has_value()) {
                break;
            }

            setRunning(nextValue.value());
        }

        co_return;
    }

    auto ConveyorBackend::consumeDamperMoveUpAsync() -> QCoro::Task<void>
    {
        while (m_damperMoveUpSubscription.isValid()) {
            auto nextValue = co_await toQCoroTask(m_damperMoveUpSubscription.stream.next());
            if (!nextValue.has_value()) {
                break;
            }

            setDamperMoveUpCommand(nextValue.value());
        }

        co_return;
    }

    auto ConveyorBackend::consumeDamperMoveDownAsync() -> QCoro::Task<void>
    {
        while (m_damperMoveDownSubscription.isValid()) {
            auto nextValue = co_await toQCoroTask(m_damperMoveDownSubscription.stream.next());
            if (!nextValue.has_value()) {
                break;
            }

            setDamperMoveDownCommand(nextValue.value());
        }

        co_return;
    }

    auto ConveyorBackend::writeBoolAsync(QString variableName, bool value) -> QCoro::Task<void>
    {
        if (!m_symbolicLink || variableName.trimmed().isEmpty()) {
            co_return;
        }

        auto writeResult = co_await toQCoroTask(m_symbolicLink->write(variableName.toStdString(), value));
        (void)writeResult;

        co_return;
    }
}