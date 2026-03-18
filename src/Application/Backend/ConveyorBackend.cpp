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
        if (m_symbolicLink && m_runSubscription.isValid()) {
            m_symbolicLink->unsubscribeRawSync(m_runSubscription.id());
        }

        m_runSubscription = {};
        m_symbolicLink = nullptr;
    }

    void ConveyorBackend::subscribeRun(core::link::ISymbolicLink* symbolicLink, const QString& variableName)
    {
        detachSymbolicLink();
        m_symbolicLink = symbolicLink;

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

    void ConveyorBackend::onSimulationTick()
    {
        const auto elapsedMs = static_cast<double>(m_stepClock.restart());
        const auto deltaSeconds = std::max(0.001, elapsedMs / 1000.0);

        if (!m_running || !m_partPresent) {
            return;
        }

        setPartPositionInternal(m_partPosition + deltaSeconds * kConveyorSpeedPerSecond);

        if (m_partPosition > kConveyorExitPosition) {
            setPartPresentInternal(false);
            updateSensorsFromPart();
            return;
        }

        updateSensorsFromPart();
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