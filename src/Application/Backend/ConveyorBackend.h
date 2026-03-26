#pragma once

#include <QCoro/QCoroTask>

#include "Link/Subscription.hpp"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <vector>

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
        Q_PROPERTY(QVariantList partPositions READ partPositions NOTIFY partPositionsChanged)
        Q_PROPERTY(bool damperOpen READ damperOpen NOTIFY damperOpenChanged)
        Q_PROPERTY(double damperPosition READ damperPosition NOTIFY damperPositionChanged)
        Q_PROPERTY(bool damperMovingUpCommand READ damperMovingUpCommand NOTIFY damperCommandsChanged)
        Q_PROPERTY(bool damperMovingDownCommand READ damperMovingDownCommand NOTIFY damperCommandsChanged)
        Q_PROPERTY(bool damperUpSensorActive READ damperUpSensorActive NOTIFY damperSensorsChanged)
        Q_PROPERTY(bool damperDownSensorActive READ damperDownSensorActive NOTIFY damperSensorsChanged)
        Q_PROPERTY(bool running READ running NOTIFY runningChanged)
        Q_PROPERTY(QVariantList sensors READ sensors NOTIFY sensorsChanged)

      public:
        explicit ConveyorBackend(QObject* parent = nullptr);
        ~ConveyorBackend() override;

        auto partPresent() const -> bool;
        auto partPosition() const -> double;
        auto partPositions() const -> QVariantList;
        auto damperOpen() const -> bool;
        auto damperPosition() const -> double;
        auto damperMovingUpCommand() const -> bool;
        auto damperMovingDownCommand() const -> bool;
        auto damperUpSensorActive() const -> bool;
        auto damperDownSensorActive() const -> bool;
        auto running() const -> bool;
        auto sensors() const -> QVariantList;

        void detachSymbolicLink();
        void resetSimulationState();
        void setSimulationEnabled(bool enabled);
        auto pollAdsStateOnce() -> QCoro::Task<void>;
        void subscribeRun(core::link::ISymbolicLink* symbolicLink, const QString& variableName);
        void configureSensorVariables(core::link::ISymbolicLink* symbolicLink,
                                      const std::array<QString, 4>& variableNames);
        void configureDamperVariables(core::link::ISymbolicLink* symbolicLink,
                                      const QString& moveUpVariable,
                                      const QString& moveDownVariable,
                                      const QString& upSensorVariable,
                                      const QString& downSensorVariable);
        auto tryPlacePartFromRobot() -> bool;
        auto tryTakePartAtExit() -> bool;

        Q_INVOKABLE void setSensorActive(int index, bool active);

      signals:
        void partPresentChanged();
        void partPositionChanged();
        void partPositionsChanged();
        void damperOpenChanged();
        void damperPositionChanged();
        void damperCommandsChanged();
        void damperSensorsChanged();
        void runningChanged();
        void sensorsChanged();

      private:
        void launchTask(QCoro::Task<void>&& task);
        void onSimulationTick();
        void unsubscribe(core::link::Subscription<bool>& subscription);
        void syncPartStateSignals(bool hadParts, double previousLeadPosition);
        void scheduleSensorOutputsWrite();
        void startSensorOutputsWrite();
        void setRunning(bool value);
        void setDamperMoveUpCommand(bool value);
        void setDamperMoveDownCommand(bool value);
        void setDamperPositionInternal(double value);
        void setSensorActiveInternal(int index, bool active);
        void publishSensorOutputs();
        void publishDamperSensorOutputs();
        void updateSensorsFromPart();
        void updateDamperSensors();
        auto flushSensorOutputsAsync() -> QCoro::Task<void>;
        auto subscribeRunAsync(QString variableName) -> QCoro::Task<void>;
        auto subscribeDamperMoveUpAsync(QString variableName) -> QCoro::Task<void>;
        auto subscribeDamperMoveDownAsync(QString variableName) -> QCoro::Task<void>;
        auto consumeRunAsync() -> QCoro::Task<void>;
        auto consumeDamperMoveUpAsync() -> QCoro::Task<void>;
        auto consumeDamperMoveDownAsync() -> QCoro::Task<void>;
        auto writeBoolAsync(QString variableName, bool value) -> QCoro::Task<void>;

        std::vector<double> m_partPositions;
        bool m_damperMoveUpCommand{ false };
        bool m_damperMoveDownCommand{ false };
        bool m_damperUpSensorActive{ false };
        bool m_damperDownSensorActive{ true };
        double m_damperPosition{ 0.0 };
        bool m_simulationEnabled{ true };
        bool m_requestedRunning{ false };
        bool m_requestedDamperMoveUpCommand{ false };
        bool m_requestedDamperMoveDownCommand{ false };
        bool m_running{ false };
        std::array<bool, 4> m_sensors{ false, false, false, false };
        QString m_runVariableName;
        std::array<QString, 4> m_sensorVariableNames;
        QString m_damperMoveUpVariableName;
        QString m_damperMoveDownVariableName;
        QString m_damperUpSensorVariableName;
        QString m_damperDownSensorVariableName;
        core::link::ISymbolicLink* m_symbolicLink{ nullptr };
        core::link::Subscription<bool> m_runSubscription;
        core::link::Subscription<bool> m_damperMoveUpSubscription;
        core::link::Subscription<bool> m_damperMoveDownSubscription;
        bool m_sensorOutputsWriteInFlight{ false };
        bool m_sensorOutputsWritePending{ false };
        size_t m_symbolicLinkGeneration{ 0 };
        QTimer* m_motionTimer{ nullptr };
        QElapsedTimer m_stepClock;
    };
}