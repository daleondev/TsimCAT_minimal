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

      public:
        explicit RotaryTableBackend(QObject* parent = nullptr);
        ~RotaryTableBackend() override;

        auto angleDegrees() const -> double;

        void subscribeActualPosition(core::link::ISymbolicLink* symbolicLink, const QString& variableName);

      signals:
        void angleDegreesChanged();

      private:
        void launchTask(QCoro::Task<void>&& task);
        void setAngleDegrees(double value);
        auto subscribeActualPositionAsync(QString variableName) -> QCoro::Task<void>;
        auto consumeActualPositionAsync() -> QCoro::Task<void>;

        double m_angleDegrees{ 0.0 };
        core::link::ISymbolicLink* m_symbolicLink{ nullptr };
        core::link::Subscription<double> m_actualPositionSubscription;
    };
}