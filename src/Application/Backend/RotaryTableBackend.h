#pragma once

#include <QCoro/QCoroTask>

#include "Link/Subscription.hpp"

#include <QObject>
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
        Q_PROPERTY(bool sensor0Active READ sensor0Active NOTIFY sensor0ActiveChanged)
        Q_PROPERTY(bool sensor180Active READ sensor180Active NOTIFY sensor180ActiveChanged)

      public:
        enum class ActualPositionType
        {
            Float,
            Double
        };

        explicit RotaryTableBackend(QObject* parent = nullptr);
        ~RotaryTableBackend() override;

        auto angleDegrees() const -> double;
        auto sensor0Active() const -> bool;
        auto sensor180Active() const -> bool;

        void detachSymbolicLink();
        void subscribeActualPosition(core::link::ISymbolicLink* symbolicLink,
                                     const QString& variableName,
                                     ActualPositionType variableType);
        void configureSensorVariables(core::link::ISymbolicLink* symbolicLink,
                                      const QString& sensor0VariableName,
                                      const QString& sensor180VariableName);

      signals:
        void angleDegreesChanged();
        void sensor0ActiveChanged();
        void sensor180ActiveChanged();

      private:
        void launchTask(QCoro::Task<void>&& task);
        void setAngleDegrees(double value);
        auto subscribeActualPositionAsync(QString variableName, ActualPositionType variableType)
          -> QCoro::Task<void>;
        auto consumeActualPositionFloatAsync() -> QCoro::Task<void>;
        auto consumeActualPositionDoubleAsync() -> QCoro::Task<void>;

        double m_angleDegrees{ 0.0 };
        bool m_sensor0Active{ false };
        bool m_sensor180Active{ false };
        core::link::ISymbolicLink* m_symbolicLink{ nullptr };
        ActualPositionType m_actualPositionType{ ActualPositionType::Double };
        core::link::Subscription<float> m_actualPositionFloatSubscription;
        core::link::Subscription<double> m_actualPositionDoubleSubscription;
    };
}