#pragma once

#include <QCoro/QCoroTask>

#include "Link/Subscription.hpp"

#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace core::link
{
    class ISymbolicLink;
}

namespace backend
{
    class RotaryTableBackend : public QObject
    {
        Q_OBJECT
        QML_NAMED_ELEMENT(RotaryTableBackend)
        QML_UNCREATABLE("Use Backend.rotaryTable")

        Q_PROPERTY(double angleDegrees READ angleDegrees NOTIFY angleDegreesChanged)
        Q_PROPERTY(bool part0Present READ part0Present NOTIFY part0PresentChanged)
        Q_PROPERTY(bool part180Present READ part180Present NOTIFY part180PresentChanged)
        Q_PROPERTY(bool sensor0Active READ sensor0Active NOTIFY sensor0ActiveChanged)
        Q_PROPERTY(bool sensor180Active READ sensor180Active NOTIFY sensor180ActiveChanged)

      public:
        enum class ActualPositionType
        {
            Float,
            Double
        };

        enum class SpawnSide
        {
            None,
            Zero,
            OneEighty
        };

        explicit RotaryTableBackend(QObject* parent = nullptr);
        ~RotaryTableBackend() override;

        auto angleDegrees() const -> double;
        auto part0Present() const -> bool;
        auto part180Present() const -> bool;
        auto sensor0Active() const -> bool;
        auto sensor180Active() const -> bool;

        void detachSymbolicLink();
        void resetSimulationState();
        void subscribeActualPosition(core::link::ISymbolicLink* symbolicLink,
                                     const QString& variableName,
                                     ActualPositionType variableType);
        void configureSensorVariables(core::link::ISymbolicLink* symbolicLink,
                                      const QString& sensor0VariableName,
                                      const QString& sensor180VariableName);
        auto tryTakePartForRobotPick() -> bool;

      signals:
        void angleDegreesChanged();
        void part0PresentChanged();
        void part180PresentChanged();
        void sensor0ActiveChanged();
        void sensor180ActiveChanged();

      private:
        void launchTask(QCoro::Task<void>&& task);
        void publishAngleDegrees(double value);
        void setAngleDegrees(double value);
        void updateSpawnState(double rawAngle);
        void resetPendingSpawn();
        void handleSpawnTimer();
        void setPart0Present(bool value);
        void setPart180Present(bool value);
        auto writeBoolAsync(QString variableName, bool value) -> QCoro::Task<void>;
        auto subscribeActualPositionAsync(QString variableName, ActualPositionType variableType)
          -> QCoro::Task<void>;
        auto consumeActualPositionFloatAsync() -> QCoro::Task<void>;
        auto consumeActualPositionDoubleAsync() -> QCoro::Task<void>;

        double m_angleDegrees{ 0.0 };
        bool m_part0Present{ false };
        bool m_part180Present{ false };
        bool m_sensor0Active{ false };
        bool m_sensor180Active{ false };
        QString m_sensor0VariableName;
        QString m_sensor180VariableName;
        core::link::ISymbolicLink* m_symbolicLink{ nullptr };
        ActualPositionType m_actualPositionType{ ActualPositionType::Double };
        QTimer* m_spawnTimer{ nullptr };
        SpawnSide m_pendingSpawnSide{ SpawnSide::None };
        double m_pendingSpawnReferenceAngle{ 0.0 };
        core::link::Subscription<float> m_actualPositionFloatSubscription;
        core::link::Subscription<double> m_actualPositionDoubleSubscription;
    };
}