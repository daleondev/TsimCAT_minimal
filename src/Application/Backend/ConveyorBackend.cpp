#include "ConveyorBackend.h"

#include "Link/Symbolic/ISymbolicLink.hpp"

#include <utility>

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
    ConveyorBackend::ConveyorBackend(QObject* parent)
      : QObject(parent)
    {
    }

    ConveyorBackend::~ConveyorBackend() { detachSymbolicLink(); }

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
    }

    void ConveyorBackend::setSensorActive(int index, bool active) { setSensorActiveInternal(index, active); }

    void ConveyorBackend::launchTask(QCoro::Task<void>&& task)
    {
        auto guarded = std::move(task).then([]() {}, [](const std::exception&) {});
        QCoro::connect(std::move(guarded), this, []() {});
    }

    void ConveyorBackend::setRunning(bool value)
    {
        if (m_running == value) {
            return;
        }

        m_running = value;
        emit runningChanged();
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