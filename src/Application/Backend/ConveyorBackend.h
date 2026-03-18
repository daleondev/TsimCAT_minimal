#pragma once

#include <QCoro/QCoroTask>

#include "Link/Subscription.hpp"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
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

        Q_PROPERTY(bool partPresent READ partPresent NOTIFY partPresentChanged)
        Q_PROPERTY(double partPosition READ partPosition NOTIFY partPositionChanged)
        Q_PROPERTY(bool running READ running NOTIFY runningChanged)
        Q_PROPERTY(QVariantList sensors READ sensors NOTIFY sensorsChanged)

      public:
        explicit ConveyorBackend(QObject* parent = nullptr);
        ~ConveyorBackend() override;

        auto partPresent() const -> bool;
        auto partPosition() const -> double;
        auto running() const -> bool;
        auto sensors() const -> QVariantList;

        void detachSymbolicLink();
        void subscribeRun(core::link::ISymbolicLink* symbolicLink, const QString& variableName);
        void configureSensorVariables(core::link::ISymbolicLink* symbolicLink,
                                      const std::array<QString, 4>& variableNames);
        auto tryPlacePartFromRobot() -> bool;
        auto tryTakePartAtExit() -> bool;

        Q_INVOKABLE void setSensorActive(int index, bool active);

      signals:
        void partPresentChanged();
        void partPositionChanged();
        void runningChanged();
        void sensorsChanged();

      private:
        void launchTask(QCoro::Task<void>&& task);
        void onSimulationTick();
        void setRunning(bool value);
        void setPartPresentInternal(bool value);
        void setPartPositionInternal(double value);
        void setSensorActiveInternal(int index, bool active);
        void updateSensorsFromPart();
        auto subscribeRunAsync(QString variableName) -> QCoro::Task<void>;
        auto consumeRunAsync() -> QCoro::Task<void>;
        auto writeBoolAsync(QString variableName, bool value) -> QCoro::Task<void>;

        bool m_partPresent{ false };
        double m_partPosition{ 0.0 };
        bool m_running{ false };
        std::array<bool, 4> m_sensors{ false, false, false, false };
        std::array<QString, 4> m_sensorVariableNames;
        core::link::ISymbolicLink* m_symbolicLink{ nullptr };
        core::link::Subscription<bool> m_runSubscription;
        QTimer* m_motionTimer{ nullptr };
        QElapsedTimer m_stepClock;
    };
}