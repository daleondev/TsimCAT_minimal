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
        Q_PROPERTY(bool manualMode READ manualMode NOTIFY manualModeChanged)
        Q_PROPERTY(double toolX READ toolX NOTIFY toolPoseChanged)
        Q_PROPERTY(double toolY READ toolY NOTIFY toolPoseChanged)
        Q_PROPERTY(double toolZ READ toolZ NOTIFY toolPoseChanged)
        Q_PROPERTY(double toolRollDegrees READ toolRollDegrees NOTIFY toolPoseChanged)
        Q_PROPERTY(double toolPitchDegrees READ toolPitchDegrees NOTIFY toolPoseChanged)
        Q_PROPERTY(double toolYawDegrees READ toolYawDegrees NOTIFY toolPoseChanged)

      public:
        enum class JobIdType
        {
            Int16,
            UInt16,
            Int32,
            UInt32
        };

        struct AdsConfig
        {
            QString jobIdVariable;
            QString actualJobIdVariable;
            JobIdType jobIdType{ JobIdType::Int32 };
            JobIdType actualJobIdType{ JobIdType::Int32 };
            std::array<QString, 4> areaFreePlcVariables;
            std::array<QString, 4> areaFreeRobotVariables;
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
        auto manualMode() const -> bool;
        auto toolX() const -> double;
        auto toolY() const -> double;
        auto toolZ() const -> double;
        auto toolRollDegrees() const -> double;
        auto toolPitchDegrees() const -> double;
        auto toolYawDegrees() const -> double;

        void configureAds(core::link::ISymbolicLink* symbolicLink, AdsConfig config);
        void detachSymbolicLink();

        Q_INVOKABLE void executeJob(int jobId);
        Q_INVOKABLE void jogJoint(int axisIndex, double deltaDegrees);
        Q_INVOKABLE bool jogCartesian(double deltaX,
                                      double deltaY,
                                      double deltaZ,
                                      double deltaRollDegrees,
                                      double deltaPitchDegrees,
                                      double deltaYawDegrees);
        Q_INVOKABLE void setManualMode(bool enabled);

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
        void manualModeChanged();
        void toolPoseChanged();

      private:
        void launchTask(QCoro::Task<void>&& task);
        void startAdsPoll();
        void applyAdsInputs(int requestedJobId, const std::array<bool, 4>& plcAreas);
        void onSimulationTick();
        void setAxisValues(const std::array<double, 6>& jointsDegrees);
        void setGripperGripped(bool value);
        void setCarriedPartVisible(bool value);
        void setGripperSensorBlocked(bool value);
        void setActiveJobId(int value);
        void setInMotion(bool value);
        void setAtHome(bool value);
        void updateToolPose();
        void updateRobotAreaState();
        void scheduleAdsStatusWrite();
        void completeCurrentMotion();
        auto moveToPoseSequence(const std::vector<RobotPose>& poses) -> bool;
        auto moveToJointTarget(const std::array<double, 6>& jointsDegrees) -> bool;
        auto planTrajectory(const std::array<double, 6>& startJoints,
                            const std::array<double, 6>& targetJoints) const
          -> std::vector<std::array<double, 6>>;
        auto writeStatusAsync(int activeJobId, std::array<bool, 4> areaFreeRobot) -> QCoro::Task<void>;
        auto pollAdsInputsAsync() -> QCoro::Task<void>;

        core::link::ISymbolicLink* m_symbolicLink{ nullptr };
        AdsConfig m_adsConfig;
        RobotKinematics m_kinematics;
        QTimer* m_simulationTimer{ nullptr };
        QTimer* m_adsPollTimer{ nullptr };
        QElapsedTimer m_stepClock;
        bool m_adsPollInFlight{ false };
        std::array<double, 6> m_jointAnglesDegrees{ 0.0, -90.0, 90.0, 0.0, 0.0, 0.0 };
        std::array<bool, 4> m_areaFreePlc{ true, true, true, true };
        std::array<bool, 4> m_areaFreeRobot{ true, true, true, true };
        RobotPose m_toolPose{ 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
        std::vector<std::array<double, 6>> m_currentTrajectory;
        size_t m_trajectoryStep{ 0 };
        int m_activeJobId{ 0 };
        int m_lastProcessedAdsJobId{ 0 };
        bool m_gripperGripped{ false };
        bool m_carriedPartVisible{ false };
        bool m_gripperSensorBlocked{ false };
        bool m_inMotion{ false };
        bool m_atHome{ true };
        bool m_manualMode{ false };
    };
}