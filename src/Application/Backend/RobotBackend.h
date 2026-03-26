#pragma once

#include "RobotKinematics.h"

#include <QCoro/QCoroTask>

#include "Link/Symbolic/ISymbolicLink.hpp"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <vector>

namespace backend
{
    class ConveyorBackend;
    class RotaryTableBackend;

    class RobotBackend : public QObject
    {
        Q_OBJECT
        QML_NAMED_ELEMENT(RobotBackend)
        QML_UNCREATABLE("Use Backend.robot")

        Q_PROPERTY(double axis1 READ axis1 NOTIFY axis1Changed)
        Q_PROPERTY(double axis2 READ axis2 NOTIFY axis2Changed)
        Q_PROPERTY(double axis3 READ axis3 NOTIFY axis3Changed)
        Q_PROPERTY(double axis4 READ axis4 NOTIFY axis4Changed)
        Q_PROPERTY(double axis5 READ axis5 NOTIFY axis5Changed)
        Q_PROPERTY(double axis6 READ axis6 NOTIFY axis6Changed)
        Q_PROPERTY(bool gripperGripped READ gripperGripped NOTIFY gripperGrippedChanged)
        Q_PROPERTY(bool carriedPartVisible READ carriedPartVisible NOTIFY carriedPartVisibleChanged)
        Q_PROPERTY(bool gripperSensorBlocked READ gripperSensorBlocked NOTIFY gripperSensorBlockedChanged)
        Q_PROPERTY(int activeJobId READ activeJobId NOTIFY activeJobIdChanged)
        Q_PROPERTY(bool inMotion READ inMotion NOTIFY inMotionChanged)
        Q_PROPERTY(bool atHome READ atHome NOTIFY atHomeChanged)

      public:
        enum class JobIdType
        {
            Int8,
            UInt8,
            Int16,
            UInt16,
            Int32,
            UInt32
        };

        enum class SequenceAction
        {
            Pick,
            Place
        };

        struct SequenceStep
        {
            enum class Kind
            {
                Pose,
                Action
            };

            Kind kind{ Kind::Pose };
            RobotPose pose{};
            SequenceAction action{ SequenceAction::Pick };
        };

        struct AdsConfig
        {
            QString jobIdVariable;
            QString actualJobIdVariable;
            QString gripperSensorVariable;
            JobIdType jobIdType{ JobIdType::Int32 };
            JobIdType actualJobIdType{ JobIdType::Int32 };
        };

        explicit RobotBackend(QObject* parent = nullptr);
        ~RobotBackend() override;

        auto axis1() const -> double;
        auto axis2() const -> double;
        auto axis3() const -> double;
        auto axis4() const -> double;
        auto axis5() const -> double;
        auto axis6() const -> double;
        auto gripperGripped() const -> bool;
        auto carriedPartVisible() const -> bool;
        auto gripperSensorBlocked() const -> bool;
        auto activeJobId() const -> int;
        auto inMotion() const -> bool;
        auto atHome() const -> bool;

        void configureAds(core::link::ISymbolicLink* symbolicLink, AdsConfig config);
        void resetSimulationState();
        void setSimulationEnabled(bool enabled);
        auto pollAdsStateOnce() -> QCoro::Task<void>;
        void setConveyorBackend(ConveyorBackend* conveyorBackend);
        void setRotaryTableBackend(RotaryTableBackend* rotaryTableBackend);
        void detachSymbolicLink();

        Q_INVOKABLE void executeJob(int jobId);

      signals:
        void axis1Changed();
        void axis2Changed();
        void axis3Changed();
        void axis4Changed();
        void axis5Changed();
        void axis6Changed();
        void gripperGrippedChanged();
        void carriedPartVisibleChanged();
        void gripperSensorBlockedChanged();
        void activeJobIdChanged();
        void inMotionChanged();
        void atHomeChanged();

      private:
        void launchTask(QCoro::Task<void>&& task);
        void startAdsPoll();
        void applyAdsInputs(int requestedJobId);
        void onSimulationTick();
        void setAxisValues(const std::array<double, 6>& jointsDegrees);
        void setGripperGripped(bool value);
        void setCarriedPartVisible(bool value);
        void setGripperSensorBlocked(bool value);
        void setActiveJobId(int value);
        void setInMotion(bool value);
        void setAtHome(bool value);
        void scheduleAdsStatusWrite();
        void startAdsStatusWrite();
        void applySequenceAction(SequenceAction action);
        void processPendingActions();
        void completeCurrentMotion();
        auto moveToPoseSequence(const std::vector<SequenceStep>& steps) -> bool;
        auto planTrajectory(const std::array<double, 6>& startJoints,
                            const std::array<double, 6>& targetJoints) const
          -> std::vector<std::array<double, 6>>;
        auto flushAdsStatusAsync() -> QCoro::Task<void>;
        auto writeStatusAsync(int activeJobId, bool gripperSensorActive, size_t adsGeneration)
          -> QCoro::Task<void>;
        auto readRequestedJobIdAsync() -> QCoro::Task<int>;
        auto pollAdsInputsAsync(size_t adsGeneration) -> QCoro::Task<void>;

        core::link::ISymbolicLink* m_symbolicLink{ nullptr };
        ConveyorBackend* m_conveyorBackend{ nullptr };
        RotaryTableBackend* m_rotaryTableBackend{ nullptr };
        AdsConfig m_adsConfig;
        RobotKinematics m_kinematics;
        QTimer* m_simulationTimer{ nullptr };
        QTimer* m_adsPollTimer{ nullptr };
        QElapsedTimer m_stepClock;
        bool m_adsPollInFlight{ false };
        bool m_adsStatusWriteInFlight{ false };
        bool m_adsStatusWritePending{ false };
        size_t m_adsGeneration{ 0 };
        std::array<double, 6> m_jointAnglesDegrees{ 0.0, -90.0, 90.0, 0.0, 0.0, 0.0 };

        struct PendingAction
        {
            size_t trajectoryIndex{ 0 };
            SequenceAction action{ SequenceAction::Pick };
        };

        std::vector<std::array<double, 6>> m_currentTrajectory;
        std::vector<PendingAction> m_pendingActions;
        size_t m_trajectoryStep{ 0 };
        int m_activeJobId{ 0 };
        int m_lastProcessedAdsJobId{ 0 };
        bool m_gripperGripped{ false };
        bool m_carriedPartVisible{ false };
        bool m_gripperSensorBlocked{ false };
        bool m_simulationEnabled{ true };
        bool m_inMotion{ false };
        bool m_atHome{ true };
    };
}