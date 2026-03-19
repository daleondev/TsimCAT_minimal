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
    constexpr double kPlaceEntryClearance = 160.0;
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

    auto ConveyorBackend::partPresent() const -> bool { return !m_partPositions.empty(); }

    auto ConveyorBackend::partPosition() const -> double
    {
        return m_partPositions.empty() ? 0.0 : m_partPositions.front();
    }

    auto ConveyorBackend::partPositions() const -> QVariantList
    {
        QVariantList values;
        values.reserve(static_cast<qsizetype>(m_partPositions.size()));
        for (const auto position : m_partPositions) {
            values.push_back(position);
        }
        return values;
    }

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
        m_sensorOutputsWriteInFlight = false;
        m_sensorOutputsWritePending = false;
        ++m_symbolicLinkGeneration;
        m_symbolicLink = nullptr;
    }

    void ConveyorBackend::resetSimulationState()
    {
        setRunning(false);
        const auto hadParts = !m_partPositions.empty();
        const auto previousLeadPosition = partPosition();
        m_partPositions.clear();
        syncPartStateSignals(hadParts, previousLeadPosition);
        setDamperMoveUpCommand(false);
        setDamperMoveDownCommand(false);
        setDamperPositionInternal(0.0);
        updateSensorsFromPart();
        publishSensorOutputs();
        publishDamperSensorOutputs();
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
        publishSensorOutputs();
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
        publishDamperSensorOutputs();

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
        const auto entryBlocked = std::ranges::any_of(m_partPositions, [](double position) {
            return std::abs(position - kPlacedPartStartPosition) < kPlaceEntryClearance;
        });
        if (entryBlocked) {
            return false;
        }

        const auto hadParts = !m_partPositions.empty();
        const auto previousLeadPosition = partPosition();
        m_partPositions.push_back(kPlacedPartStartPosition);
        std::ranges::sort(m_partPositions);
        syncPartStateSignals(hadParts, previousLeadPosition);
        updateSensorsFromPart();
        return true;
    }

    auto ConveyorBackend::tryTakePartAtExit() -> bool
    {
        if (m_partPositions.empty()) {
            return false;
        }

        const auto exitPart = std::ranges::find_if(
          m_partPositions, [](double position) { return position >= kTakeAtExitThreshold; });
        if (exitPart == m_partPositions.end()) {
            return false;
        }

        const auto hadParts = true;
        const auto previousLeadPosition = partPosition();
        m_partPositions.erase(exitPart);
        syncPartStateSignals(hadParts, previousLeadPosition);
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

        if (m_running && !m_partPositions.empty()) {
            const auto hadParts = true;
            const auto previousLeadPosition = partPosition();
            for (auto& position : m_partPositions) {
                position += deltaSeconds * kConveyorSpeedPerSecond;
            }

            std::erase_if(m_partPositions, [](double position) { return position > kConveyorExitPosition; });
            syncPartStateSignals(hadParts, previousLeadPosition);
            updateSensorsFromPart();
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

    void ConveyorBackend::syncPartStateSignals(bool hadParts, double previousLeadPosition)
    {
        const auto hasParts = !m_partPositions.empty();
        if (hadParts != hasParts) {
            emit partPresentChanged();
        }

        const auto nextLeadPosition = partPosition();
        if (!qFuzzyCompare(previousLeadPosition, nextLeadPosition)) {
            emit partPositionChanged();
        }

        emit partPositionsChanged();
    }

    void ConveyorBackend::scheduleSensorOutputsWrite()
    {
        if (!m_symbolicLink) {
            return;
        }

        m_sensorOutputsWritePending = true;
        if (m_sensorOutputsWriteInFlight) {
            return;
        }

        startSensorOutputsWrite();
    }

    void ConveyorBackend::startSensorOutputsWrite()
    {
        if (!m_symbolicLink || m_sensorOutputsWriteInFlight || !m_sensorOutputsWritePending) {
            return;
        }

        m_sensorOutputsWriteInFlight = true;
        launchTask(flushSensorOutputsAsync());
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
        scheduleSensorOutputsWrite();
    }

    void ConveyorBackend::publishSensorOutputs()
    {
        if (!m_symbolicLink) {
            return;
        }

        m_sensorOutputsWritePending = false;
        for (size_t index = 0; index < m_sensors.size(); ++index) {
            const auto& variableName = m_sensorVariableNames[index];
            if (!variableName.trimmed().isEmpty()) {
                launchTask(writeBoolAsync(variableName, m_sensors[index]));
            }
        }
    }

    void ConveyorBackend::publishDamperSensorOutputs()
    {
        if (m_symbolicLink && !m_damperUpSensorVariableName.trimmed().isEmpty()) {
            launchTask(writeBoolAsync(m_damperUpSensorVariableName, m_damperUpSensorActive));
        }

        if (m_symbolicLink && !m_damperDownSensorVariableName.trimmed().isEmpty()) {
            launchTask(writeBoolAsync(m_damperDownSensorVariableName, m_damperDownSensorActive));
        }
    }

    void ConveyorBackend::updateSensorsFromPart()
    {
        for (size_t index = 0; index < m_sensors.size(); ++index) {
            const auto sensorActive = std::ranges::any_of(m_partPositions, [index](double position) {
                return std::abs(position - kSensorPositions[index]) <= kPartHalfLength;
            });
            setSensorActiveInternal(static_cast<int>(index), sensorActive);
        }
    }

    auto ConveyorBackend::flushSensorOutputsAsync() -> QCoro::Task<void>
    {
        while (m_symbolicLink && m_sensorOutputsWritePending) {
            m_sensorOutputsWritePending = false;
            const auto sensorStates = m_sensors;
            const auto variableNames = m_sensorVariableNames;
            const auto generation = m_symbolicLinkGeneration;

            for (size_t index = 0; index < sensorStates.size(); ++index) {
                if (!m_symbolicLink || generation != m_symbolicLinkGeneration) {
                    m_sensorOutputsWriteInFlight = false;
                    co_return;
                }

                const auto& variableName = variableNames[index];
                if (variableName.trimmed().isEmpty()) {
                    continue;
                }

                co_await writeBoolAsync(variableName, sensorStates[index]);
            }
        }

        m_sensorOutputsWriteInFlight = false;

        if (m_symbolicLink && m_sensorOutputsWritePending) {
            startSensorOutputsWrite();
        }

        co_return;
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