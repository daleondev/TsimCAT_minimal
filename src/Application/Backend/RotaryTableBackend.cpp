#include "RotaryTableBackend.h"

#include "Link/Symbolic/ISymbolicLink.hpp"

#include <QMetaObject>
#include <QThread>

#include <cmath>
#include <iostream>
#include <utility>

namespace
{
    constexpr double kSpawnAngleToleranceDegrees = 1.0;
    constexpr double kMovementToleranceDegrees = 0.05;
    constexpr int kSpawnDelayMs = 1000;

    auto normalizeAngleDegrees(double angleDegrees) -> double
    {
        auto normalized = std::fmod(angleDegrees, 360.0);
        if (normalized < 0.0) {
            normalized += 360.0;
        }

        return normalized;
    }

    auto circularDistanceDegrees(double lhs, double rhs) -> double
    {
        const auto delta = std::abs(normalizeAngleDegrees(lhs) - normalizeAngleDegrees(rhs));
        return std::min(delta, 360.0 - delta);
    }

    auto parseActualPositionType(backend::RotaryTableBackend::ActualPositionType type) -> const char*
    {
        switch (type) {
            case backend::RotaryTableBackend::ActualPositionType::Float:
                return "float";
            case backend::RotaryTableBackend::ActualPositionType::Double:
                return "double";
        }

        return "unknown";
    }

    auto detectSpawnSide(double rawAngle) -> backend::RotaryTableBackend::SpawnSide
    {
        const auto normalizedAngle = normalizeAngleDegrees(rawAngle);

        if (circularDistanceDegrees(normalizedAngle, 0.0) <= kSpawnAngleToleranceDegrees) {
            return backend::RotaryTableBackend::SpawnSide::Zero;
        }

        if (circularDistanceDegrees(normalizedAngle, 180.0) <= kSpawnAngleToleranceDegrees) {
            return backend::RotaryTableBackend::SpawnSide::OneEighty;
        }

        return backend::RotaryTableBackend::SpawnSide::None;
    }

    auto spawnSideName(backend::RotaryTableBackend::SpawnSide side) -> const char*
    {
        switch (side) {
            case backend::RotaryTableBackend::SpawnSide::Zero:
                return "Zero";
            case backend::RotaryTableBackend::SpawnSide::OneEighty:
                return "OneEighty";
            case backend::RotaryTableBackend::SpawnSide::None:
                return "None";
        }

        return "Unknown";
    }

    template<typename T>
    auto toQCoroTask(core::coro::Task<T>&& task) -> QCoro::Task<T>
    {
        co_return co_await std::move(task);
    }
}

namespace backend
{
    RotaryTableBackend::RotaryTableBackend(QObject* parent)
      : QObject(parent)
      , m_spawnTimer(new QTimer(this))
    {
        m_spawnTimer->setSingleShot(true);
        connect(m_spawnTimer, &QTimer::timeout, this, &RotaryTableBackend::handleSpawnTimer);
    }

    RotaryTableBackend::~RotaryTableBackend() { detachSymbolicLink(); }

    auto RotaryTableBackend::angleDegrees() const -> double { return m_angleDegrees; }

    auto RotaryTableBackend::part0Present() const -> bool { return m_part0Present; }

    auto RotaryTableBackend::part180Present() const -> bool { return m_part180Present; }

    auto RotaryTableBackend::sensor0Active() const -> bool { return m_sensor0Active; }

    auto RotaryTableBackend::sensor180Active() const -> bool { return m_sensor180Active; }

    void RotaryTableBackend::detachSymbolicLink()
    {
        resetPendingSpawn();

        if (m_symbolicLink && m_actualPositionFloatSubscription.isValid()) {
            m_symbolicLink->unsubscribeRawSync(m_actualPositionFloatSubscription.id());
        }

        if (m_symbolicLink && m_actualPositionDoubleSubscription.isValid()) {
            m_symbolicLink->unsubscribeRawSync(m_actualPositionDoubleSubscription.id());
        }

        m_actualPositionFloatSubscription = {};
        m_actualPositionDoubleSubscription = {};
        m_actualPositionVariableName.clear();
        m_symbolicLink = nullptr;
    }

    void RotaryTableBackend::resetSimulationState()
    {
        resetPendingSpawn();
        setPart0Present(false);
        setPart180Present(false);
    }

    void RotaryTableBackend::setSimulationEnabled(bool enabled)
    {
        if (m_simulationEnabled == enabled) {
            return;
        }

        m_simulationEnabled = enabled;
        logState(m_simulationEnabled ? "rotary_enable_before_restart" : "rotary_disable");

        if (!m_simulationEnabled) {
            resetPendingSpawn();
            logState("rotary_disable_after_resetPendingSpawn");
            return;
        }

        updateSpawnState(m_angleDegrees);
        logState("rotary_enable_after_restart");
    }

    auto RotaryTableBackend::pollAdsStateOnce() -> QCoro::Task<void>
    {
        if (!m_symbolicLink || m_actualPositionVariableName.trimmed().isEmpty()) {
            co_return;
        }

        const auto applyAngleSynchronously = [this](double value) {
            if (QThread::currentThread() == thread()) {
                setAngleDegrees(value);
                return;
            }

            QMetaObject::invokeMethod(
              this, [this, value]() { setAngleDegrees(value); }, Qt::BlockingQueuedConnection);
        };

        switch (m_actualPositionType) {
            case ActualPositionType::Float: {
                const auto result = co_await toQCoroTask(
                  m_symbolicLink->read<float>(m_actualPositionVariableName.toStdString()));
                if (result) {
                    std::cout << "[reset-trace] rotary_poll value=" << static_cast<double>(result.value())
                              << " storedAngleBefore=" << m_angleDegrees << std::endl;
                    applyAngleSynchronously(static_cast<double>(result.value()));
                    logState("rotary_poll_after_apply");
                }
                break;
            }
            case ActualPositionType::Double: {
                const auto result = co_await toQCoroTask(
                  m_symbolicLink->read<double>(m_actualPositionVariableName.toStdString()));
                if (result) {
                    std::cout << "[reset-trace] rotary_poll value=" << result.value()
                              << " storedAngleBefore=" << m_angleDegrees << std::endl;
                    applyAngleSynchronously(result.value());
                    logState("rotary_poll_after_apply");
                }
                break;
            }
        }

        co_return;
    }

    void RotaryTableBackend::subscribeActualPosition(core::link::ISymbolicLink* symbolicLink,
                                                     const QString& variableName,
                                                     ActualPositionType variableType)
    {
        detachSymbolicLink();
        m_symbolicLink = symbolicLink;
        m_actualPositionType = variableType;
        m_actualPositionVariableName = variableName;

        if (!m_symbolicLink || variableName.trimmed().isEmpty()) {
            return;
        }

        launchTask(subscribeActualPositionAsync(variableName, variableType));
    }

    void RotaryTableBackend::configureSensorVariables(core::link::ISymbolicLink* symbolicLink,
                                                      const QString& sensor0VariableName,
                                                      const QString& sensor180VariableName)
    {
        if (symbolicLink) {
            m_symbolicLink = symbolicLink;
        }

        m_sensor0VariableName = sensor0VariableName;
        m_sensor180VariableName = sensor180VariableName;

        if (m_symbolicLink && !m_sensor0VariableName.trimmed().isEmpty()) {
            launchTask(writeBoolAsync(m_sensor0VariableName, m_sensor0Active));
        }

        if (m_symbolicLink && !m_sensor180VariableName.trimmed().isEmpty()) {
            launchTask(writeBoolAsync(m_sensor180VariableName, m_sensor180Active));
        }
    }

    auto RotaryTableBackend::tryTakePartForRobotPick() -> bool
    {
        if (!m_simulationEnabled) {
            return false;
        }

        const auto side = detectSpawnSide(m_angleDegrees);

        if (side == SpawnSide::Zero && m_part180Present) {
            setPart180Present(false);
            return true;
        }

        if (side == SpawnSide::OneEighty && m_part0Present) {
            setPart0Present(false);
            return true;
        }

        return false;
    }

    void RotaryTableBackend::launchTask(QCoro::Task<void>&& task)
    {
        auto guarded = std::move(task).then([]() {}, [](const std::exception&) {});
        QCoro::connect(std::move(guarded), this, []() {});
    }

    void RotaryTableBackend::publishAngleDegrees(double value)
    {
        if (QThread::currentThread() == thread()) {
            setAngleDegrees(value);
            return;
        }

        QMetaObject::invokeMethod(this, [this, value]() { setAngleDegrees(value); }, Qt::QueuedConnection);
    }

    void RotaryTableBackend::setAngleDegrees(double value)
    {
        if (m_simulationEnabled) {
            updateSpawnState(value);
        }
        else {
            resetPendingSpawn();
        }

        const auto angleChanged = !qFuzzyCompare(m_angleDegrees, value);

        if (angleChanged) {
            m_angleDegrees = value;
            emit angleDegreesChanged();
        }
    }

    void RotaryTableBackend::updateSpawnState(double rawAngle)
    {
        const auto normalizedAngle = normalizeAngleDegrees(rawAngle);
        const auto candidate = detectSpawnSide(normalizedAngle);

        if (candidate == SpawnSide::None) {
            resetPendingSpawn();
            return;
        }

        if ((candidate == SpawnSide::Zero && m_part0Present) ||
            (candidate == SpawnSide::OneEighty && m_part180Present)) {
            resetPendingSpawn();
            return;
        }

        if (m_pendingSpawnSide != candidate) {
            m_pendingSpawnSide = candidate;
            m_pendingSpawnReferenceAngle = normalizedAngle;
            m_spawnTimer->start(kSpawnDelayMs);
            return;
        }

        if (circularDistanceDegrees(normalizedAngle, m_pendingSpawnReferenceAngle) >
            kMovementToleranceDegrees) {
            m_pendingSpawnReferenceAngle = normalizedAngle;
            m_spawnTimer->start(kSpawnDelayMs);
        }
    }

    void RotaryTableBackend::resetPendingSpawn()
    {
        if (m_spawnTimer) {
            m_spawnTimer->stop();
        }

        m_pendingSpawnSide = SpawnSide::None;
    }

    void RotaryTableBackend::logState(const char* context) const
    {
        std::cout << "[reset-trace] " << context << " angle=" << m_angleDegrees << " part0=" << m_part0Present
                  << " part180=" << m_part180Present << " sensor0=" << m_sensor0Active
                  << " sensor180=" << m_sensor180Active << " simulationEnabled=" << m_simulationEnabled
                  << " pendingSpawn=" << spawnSideName(m_pendingSpawnSide)
                  << " pendingReference=" << m_pendingSpawnReferenceAngle << std::endl;
    }

    void RotaryTableBackend::handleSpawnTimer()
    {
        switch (m_pendingSpawnSide) {
            case SpawnSide::Zero:
                setPart0Present(true);
                break;
            case SpawnSide::OneEighty:
                setPart180Present(true);
                break;
            case SpawnSide::None:
                break;
        }

        resetPendingSpawn();
    }

    void RotaryTableBackend::setPart0Present(bool value)
    {
        if (m_part0Present == value && m_sensor0Active == value) {
            return;
        }

        const auto partChanged = m_part0Present != value;
        const auto sensorChanged = m_sensor0Active != value;

        m_part0Present = value;
        m_sensor0Active = value;

        if (partChanged) {
            emit part0PresentChanged();
        }

        if (sensorChanged) {
            emit sensor0ActiveChanged();

            if (m_symbolicLink && !m_sensor0VariableName.trimmed().isEmpty()) {
                launchTask(writeBoolAsync(m_sensor0VariableName, m_sensor0Active));
            }
        }
    }

    void RotaryTableBackend::setPart180Present(bool value)
    {
        if (m_part180Present == value && m_sensor180Active == value) {
            return;
        }

        const auto partChanged = m_part180Present != value;
        const auto sensorChanged = m_sensor180Active != value;

        m_part180Present = value;
        m_sensor180Active = value;

        if (partChanged) {
            emit part180PresentChanged();
        }

        if (sensorChanged) {
            emit sensor180ActiveChanged();

            if (m_symbolicLink && !m_sensor180VariableName.trimmed().isEmpty()) {
                launchTask(writeBoolAsync(m_sensor180VariableName, m_sensor180Active));
            }
        }
    }

    auto RotaryTableBackend::writeBoolAsync(QString variableName, bool value) -> QCoro::Task<void>
    {
        if (!m_symbolicLink || variableName.trimmed().isEmpty()) {
            co_return;
        }

        auto writeResult = co_await toQCoroTask(m_symbolicLink->write(variableName.toStdString(), value));
        (void)writeResult;

        co_return;
    }

    auto RotaryTableBackend::subscribeActualPositionAsync(QString variableName,
                                                          ActualPositionType variableType)
      -> QCoro::Task<void>
    {
        if (!m_symbolicLink || variableName.trimmed().isEmpty()) {
            co_return;
        }

        switch (variableType) {
            case ActualPositionType::Float: {
                auto subscriptionResult =
                  co_await toQCoroTask(m_symbolicLink->subscribe<float>(variableName.toStdString()));
                if (!subscriptionResult) {
                    co_return;
                }

                m_actualPositionFloatSubscription = std::move(subscriptionResult).value();
                launchTask(consumeActualPositionFloatAsync());
                break;
            }
            case ActualPositionType::Double: {
                auto subscriptionResult =
                  co_await toQCoroTask(m_symbolicLink->subscribe<double>(variableName.toStdString()));
                if (!subscriptionResult) {
                    co_return;
                }

                m_actualPositionDoubleSubscription = std::move(subscriptionResult).value();
                launchTask(consumeActualPositionDoubleAsync());
                break;
            }
        }

        co_return;
    }

    auto RotaryTableBackend::consumeActualPositionFloatAsync() -> QCoro::Task<void>
    {
        while (m_actualPositionFloatSubscription.isValid()) {
            auto nextValue = co_await toQCoroTask(m_actualPositionFloatSubscription.stream.next());
            if (!nextValue.has_value()) {
                break;
            }

            std::cout << "Received angle (" << parseActualPositionType(ActualPositionType::Float)
                      << "): " << nextValue.value() << std::endl;
            publishAngleDegrees(static_cast<double>(nextValue.value()));
        }

        co_return;
    }

    auto RotaryTableBackend::consumeActualPositionDoubleAsync() -> QCoro::Task<void>
    {
        while (m_actualPositionDoubleSubscription.isValid()) {
            auto nextValue = co_await toQCoroTask(m_actualPositionDoubleSubscription.stream.next());
            if (!nextValue.has_value()) {
                break;
            }

            std::cout << "Received angle (" << parseActualPositionType(ActualPositionType::Double)
                      << "): " << nextValue.value() << std::endl;
            publishAngleDegrees(nextValue.value());
        }

        co_return;
    }
}