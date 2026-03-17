#include "RotaryTableBackend.h"

#include "Link/Symbolic/ISymbolicLink.hpp"

#include <cmath>
#include <utility>

namespace
{
    constexpr double kSensorToleranceDegrees = 1.0;

    auto normalizeAngleDegrees(double angleDegrees) -> double
    {
        auto normalized = std::fmod(angleDegrees, 360.0);
        if (normalized < 0.0) {
            normalized += 360.0;
        }
        return normalized;
    }

    auto isAngleNear(double angleDegrees, double targetDegrees) -> bool
    {
        const auto delta = std::abs(normalizeAngleDegrees(angleDegrees) - targetDegrees);
        return std::min(delta, 360.0 - delta) <= kSensorToleranceDegrees;
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
    {
    }

    RotaryTableBackend::~RotaryTableBackend() { detachSymbolicLink(); }

    auto RotaryTableBackend::angleDegrees() const -> double { return m_angleDegrees; }

    auto RotaryTableBackend::sensor0Active() const -> bool { return m_sensor0Active; }

    auto RotaryTableBackend::sensor180Active() const -> bool { return m_sensor180Active; }

    void RotaryTableBackend::detachSymbolicLink()
    {
        if (m_symbolicLink && m_actualPositionSubscription.isValid()) {
            m_symbolicLink->unsubscribeRawSync(m_actualPositionSubscription.id());
        }

        m_actualPositionSubscription = {};
        m_symbolicLink = nullptr;
    }

    void RotaryTableBackend::subscribeActualPosition(core::link::ISymbolicLink* symbolicLink,
                                                     const QString& variableName)
    {
        detachSymbolicLink();
        m_symbolicLink = symbolicLink;

        if (!m_symbolicLink || variableName.trimmed().isEmpty()) {
            return;
        }

        launchTask(subscribeActualPositionAsync(variableName));
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

        if (m_hasAngleReading) {
            syncDerivedSensors(m_angleDegrees);
        }
    }

    void RotaryTableBackend::launchTask(QCoro::Task<void>&& task)
    {
        auto guarded = std::move(task).then([]() {}, [](const std::exception&) {});
        QCoro::connect(std::move(guarded), this, []() {});
    }

    void RotaryTableBackend::setAngleDegrees(double value)
    {
        const auto angleChanged = !qFuzzyCompare(m_angleDegrees, value);

        m_hasAngleReading = true;
        syncDerivedSensors(value);

        if (angleChanged) {
            m_angleDegrees = value;
            emit angleDegreesChanged();
        }
    }

    void RotaryTableBackend::setSensor0Active(bool value)
    {
        if (m_sensor0Active == value) {
            return;
        }

        m_sensor0Active = value;
        emit sensor0ActiveChanged();

        if (m_symbolicLink && !m_sensor0VariableName.trimmed().isEmpty()) {
            launchTask(writeBoolAsync(m_sensor0VariableName, value));
        }
    }

    void RotaryTableBackend::setSensor180Active(bool value)
    {
        if (m_sensor180Active == value) {
            return;
        }

        m_sensor180Active = value;
        emit sensor180ActiveChanged();

        if (m_symbolicLink && !m_sensor180VariableName.trimmed().isEmpty()) {
            launchTask(writeBoolAsync(m_sensor180VariableName, value));
        }
    }

    void RotaryTableBackend::syncDerivedSensors(double angleDegrees)
    {
        setSensor0Active(isAngleNear(angleDegrees, 0.0));
        setSensor180Active(isAngleNear(angleDegrees, 180.0));
    }

    auto RotaryTableBackend::subscribeActualPositionAsync(QString variableName) -> QCoro::Task<void>
    {
        if (!m_symbolicLink || variableName.trimmed().isEmpty()) {
            co_return;
        }

        auto subscriptionResult =
          co_await toQCoroTask(m_symbolicLink->subscribe<double>(variableName.toStdString()));
        if (!subscriptionResult) {
            co_return;
        }

        m_actualPositionSubscription = std::move(subscriptionResult).value();
        launchTask(consumeActualPositionAsync());
    }

    auto RotaryTableBackend::consumeActualPositionAsync() -> QCoro::Task<void>
    {
        while (m_actualPositionSubscription.isValid()) {
            auto nextValue = co_await toQCoroTask(m_actualPositionSubscription.stream.next());
            if (!nextValue.has_value()) {
                break;
            }

            setAngleDegrees(nextValue.value());
        }

        co_return;
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
}