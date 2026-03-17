#pragma once

#include <QCoro/QCoroTask>

#include "Link/Subscription.hpp"

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <array>

namespace core::link
{
    class ISymbolicLink;
}

namespace backend
{
    class ConveyorBackend : public QObject
    {
        Q_OBJECT
        QML_NAMED_ELEMENT(ConveyorBackend)
        QML_UNCREATABLE("Use Backend.conveyor")

        Q_PROPERTY(bool running READ running NOTIFY runningChanged)
        Q_PROPERTY(QVariantList sensors READ sensors NOTIFY sensorsChanged)

      public:
        explicit ConveyorBackend(QObject* parent = nullptr);
        ~ConveyorBackend() override;

        auto running() const -> bool;
        auto sensors() const -> QVariantList;

        void detachSymbolicLink();
        void subscribeRun(core::link::ISymbolicLink* symbolicLink, const QString& variableName);
        void configureSensorVariables(core::link::ISymbolicLink* symbolicLink,
                                      const std::array<QString, 4>& variableNames);

        Q_INVOKABLE void setSensorActive(int index, bool active);

      signals:
        void runningChanged();
        void sensorsChanged();

      private:
        void launchTask(QCoro::Task<void>&& task);
        void setRunning(bool value);
        void setSensorActiveInternal(int index, bool active);
        auto subscribeRunAsync(QString variableName) -> QCoro::Task<void>;
        auto consumeRunAsync() -> QCoro::Task<void>;
        auto writeBoolAsync(QString variableName, bool value) -> QCoro::Task<void>;

        bool m_running{ false };
        std::array<bool, 4> m_sensors{ false, false, false, false };
        std::array<QString, 4> m_sensorVariableNames;
        core::link::ISymbolicLink* m_symbolicLink{ nullptr };
        core::link::Subscription<bool> m_runSubscription;
    };
}