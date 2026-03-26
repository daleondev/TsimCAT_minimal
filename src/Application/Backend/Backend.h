#pragma once

#include "AdsConfigBackend.h"
#include "ConveyorBackend.h"
#include "RobotBackend.h"
#include "RotaryTableBackend.h"

#include <QCoro/QCoroTask>

#include <QObject>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <memory>

namespace core::link
{
    class ILink;
    class ISymbolicLink;
}

namespace backend
{
    class Backend : public QObject
    {
        Q_OBJECT
        QML_ELEMENT
        Q_PROPERTY(backend::AdsConfigBackend* adsConfig READ adsConfig CONSTANT)
        Q_PROPERTY(backend::ConveyorBackend* conveyor READ conveyor CONSTANT)
        Q_PROPERTY(backend::RobotBackend* robot READ robot CONSTANT)
        Q_PROPERTY(backend::RotaryTableBackend* rotaryTable READ rotaryTable CONSTANT)
        Q_PROPERTY(QString welcomeMessage READ welcomeMessage CONSTANT)

      public:
        explicit Backend(QObject* parent = nullptr);
        ~Backend() override;

        auto adsConfig() const -> AdsConfigBackend*;
        auto conveyor() const -> ConveyorBackend*;
        auto robot() const -> RobotBackend*;
        auto rotaryTable() const -> RotaryTableBackend*;
        QString welcomeMessage() const;

        Q_INVOKABLE void resetSimulationState();
        Q_INVOKABLE void resyncAds();

      private:
        struct SharedAdsConfig;

        void launchTask(QCoro::Task<void>&& task);
        void setSimulationEnabled(bool enabled);
        void resetSimulationResetControl();
        void startSimulationResetPoll();
        void applySimulationResetCommand(bool resetCommand);
        auto refreshPlcSignalsOnceAsync(bool enableSimulationBeforeRobotPoll) -> QCoro::Task<void>;
        auto initializeSharedAdsLinkAsync(bool preserveResetState = false) -> QCoro::Task<void>;
        auto configFilePath() const -> QString;
        auto loadSharedAdsConfig() const -> SharedAdsConfig;
        auto pollSimulationResetAsync(QString variableName, size_t generation) -> QCoro::Task<void>;

        AdsConfigBackend* m_adsConfig{ nullptr };
        ConveyorBackend* m_conveyor{ nullptr };
        RobotBackend* m_robot{ nullptr };
        RotaryTableBackend* m_rotaryTable{ nullptr };
        std::unique_ptr<core::link::ILink> m_sharedAdsLink;
        core::link::ISymbolicLink* m_sharedAdsSymbolicLink{ nullptr };
        QTimer* m_simulationResetPollTimer{ nullptr };
        QString m_simulationResetVariableName;
        bool m_simulationResetPollInFlight{ false };
        size_t m_simulationResetGeneration{ 0 };
        bool m_lastSimulationResetCommand{ false };
    };
}
