#include "RotaryTableBackend.h"

#include "Link/Symbolic/ISymbolicLink.hpp"

#include <iostream>
#include <utility>

namespace
{
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
        if (m_symbolicLink && m_actualPositionFloatSubscription.isValid()) {
            m_symbolicLink->unsubscribeRawSync(m_actualPositionFloatSubscription.id());
        }

        if (m_symbolicLink && m_actualPositionDoubleSubscription.isValid()) {
            m_symbolicLink->unsubscribeRawSync(m_actualPositionDoubleSubscription.id());
        }

        m_actualPositionFloatSubscription = {};
        m_actualPositionDoubleSubscription = {};
        m_symbolicLink = nullptr;
    }

    void RotaryTableBackend::subscribeActualPosition(core::link::ISymbolicLink* symbolicLink,
                                                     const QString& variableName,
                                                     ActualPositionType variableType)
    {
        detachSymbolicLink();
        m_symbolicLink = symbolicLink;
        m_actualPositionType = variableType;

        if (!m_symbolicLink || variableName.trimmed().isEmpty()) {
            return;
        }

        launchTask(subscribeActualPositionAsync(variableName, variableType));
    }

    void RotaryTableBackend::configureSensorVariables(core::link::ISymbolicLink* symbolicLink,
                                                      const QString& sensor0VariableName,
                                                      const QString& sensor180VariableName)
    {
        Q_UNUSED(symbolicLink);
        Q_UNUSED(sensor0VariableName);
        Q_UNUSED(sensor180VariableName);
    }

    void RotaryTableBackend::launchTask(QCoro::Task<void>&& task)
    {
        auto guarded = std::move(task).then([]() {}, [](const std::exception&) {});
        QCoro::connect(std::move(guarded), this, []() {});
    }

    void RotaryTableBackend::setAngleDegrees(double value)
    {
        const auto angleChanged = !qFuzzyCompare(m_angleDegrees, value);

        if (angleChanged) {
            m_angleDegrees = value;
            emit angleDegreesChanged();
        }
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
            setAngleDegrees(static_cast<double>(nextValue.value()));
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
            setAngleDegrees(nextValue.value());
        }

        co_return;
    }
}