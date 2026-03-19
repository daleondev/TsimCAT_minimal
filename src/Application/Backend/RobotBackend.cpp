#include "RobotBackend.h"

#include "ConveyorBackend.h"
#include "RotaryTableBackend.h"

#include <QMetaObject>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <utility>

namespace
{
    constexpr std::array<double, 6> kHomeJointsDegrees = { 0.0, -90.0, 90.0, 0.0, 0.0, 0.0 };
    constexpr std::array<double, 6> kJointLowerBounds = { -180.0, -150.0, -150.0, -180.0, -125.0, -180.0 };
    constexpr std::array<double, 6> kJointUpperBounds = { 180.0, 150.0, 150.0, 180.0, 125.0, 180.0 };
    constexpr std::array<bool, 6> kWrapJoints = { true, false, false, true, false, true };
    constexpr double kJointSpeedDegreesPerSecond = 150.0;

    template<typename T>
    auto toQCoroTask(core::coro::Task<T>&& task) -> QCoro::Task<T>
    {
        co_return co_await std::move(task);
    }

    auto degreesToRadians(double value) -> double { return value * std::numbers::pi / 180.0; }

    auto radiansToDegrees(double value) -> double { return value * 180.0 / std::numbers::pi; }

    auto jointsToRadians(const std::array<double, 6>& jointsDegrees) -> std::array<double, 6>
    {
        std::array<double, 6> result{};
        for (size_t index = 0; index < result.size(); ++index) {
            result[index] = degreesToRadians(jointsDegrees[index]);
        }
        return result;
    }

    auto normalizeJointToBounds(double value, size_t axis) -> double
    {
        const double low = kJointLowerBounds[axis];
        const double high = kJointUpperBounds[axis];

        if (kWrapJoints[axis]) {
            while (value < low) {
                value += 360.0;
            }
            while (value > high) {
                value -= 360.0;
            }
        }

        return std::clamp(value, low, high);
    }

    auto shortestWrappedDelta(double start, double target, size_t axis) -> double
    {
        double delta = target - start;
        if (!kWrapJoints[axis]) {
            return delta;
        }

        while (delta > 180.0) {
            delta -= 360.0;
        }
        while (delta < -180.0) {
            delta += 360.0;
        }
        return delta;
    }

    auto isHomeConfiguration(const std::array<double, 6>& jointsDegrees) -> bool
    {
        for (size_t index = 0; index < jointsDegrees.size(); ++index) {
            if (std::abs(jointsDegrees[index] - kHomeJointsDegrees[index]) > 0.1) {
                return false;
            }
        }

        return true;
    }

    auto poseStep(const backend::RobotPose& pose) -> backend::RobotBackend::SequenceStep
    {
        return backend::RobotBackend::SequenceStep{
            .kind = backend::RobotBackend::SequenceStep::Kind::Pose,
            .pose = pose,
            .action = backend::RobotBackend::SequenceAction::Pick,
        };
    }

    auto actionStep(backend::RobotBackend::SequenceAction action) -> backend::RobotBackend::SequenceStep
    {
        return backend::RobotBackend::SequenceStep{
            .kind = backend::RobotBackend::SequenceStep::Kind::Action,
            .pose = {},
            .action = action,
        };
    }

    auto jobPoses(int jobId) -> std::vector<backend::RobotBackend::SequenceStep>
    {
        using backend::RobotPose;
        using SequenceAction = backend::RobotBackend::SequenceAction;

        const RobotPose home{ 800.0, 0.0, 900.0, 0.0, std::numbers::pi / 2.0, 0.0 };
        const RobotPose pickTable{ 615.0, 650.0, 520.0, 0.0, 0.0, std::numbers::pi / 2.0 };
        const RobotPose placeConveyor{ 615.0, -690.0, 520.0, 0.0, 0.0, -std::numbers::pi / 2.0 };

        switch (jobId) {
            case 1:
                return { poseStep(home) };
            case 2:
                return {
                    poseStep(home), poseStep(pickTable), actionStep(SequenceAction::Pick), poseStep(home)
                };
            case 5:
                return {
                    poseStep(home), poseStep(placeConveyor), actionStep(SequenceAction::Place), poseStep(home)
                };
            default:
                return {};
        }
    }
}

namespace backend
{
    RobotBackend::RobotBackend(QObject* parent)
      : QObject(parent)
      , m_simulationTimer(new QTimer(this))
      , m_adsPollTimer(new QTimer(this))
    {
        m_stepClock.start();
        updateToolPose();

        m_simulationTimer->setInterval(16);
        connect(m_simulationTimer, &QTimer::timeout, this, &RobotBackend::onSimulationTick);
        m_simulationTimer->start();

        m_adsPollTimer->setInterval(100);
        connect(m_adsPollTimer, &QTimer::timeout, this, &RobotBackend::startAdsPoll);
    }

    RobotBackend::~RobotBackend() { detachSymbolicLink(); }

    auto RobotBackend::axis1() const -> double { return m_jointAnglesDegrees[0]; }
    auto RobotBackend::axis2() const -> double { return m_jointAnglesDegrees[1]; }
    auto RobotBackend::axis3() const -> double { return m_jointAnglesDegrees[2]; }
    auto RobotBackend::axis4() const -> double { return m_jointAnglesDegrees[3]; }
    auto RobotBackend::axis5() const -> double { return m_jointAnglesDegrees[4]; }
    auto RobotBackend::axis6() const -> double { return m_jointAnglesDegrees[5]; }
    auto RobotBackend::gripperGripped() const -> bool { return m_gripperGripped; }
    auto RobotBackend::carriedPartVisible() const -> bool { return m_carriedPartVisible; }
    auto RobotBackend::gripperSensorBlocked() const -> bool { return m_gripperSensorBlocked; }
    auto RobotBackend::activeJobId() const -> int { return m_activeJobId; }
    auto RobotBackend::inMotion() const -> bool { return m_inMotion; }
    auto RobotBackend::atHome() const -> bool { return m_atHome; }
    auto RobotBackend::manualMode() const -> bool { return m_manualMode; }
    auto RobotBackend::toolX() const -> double { return m_toolPose.x; }
    auto RobotBackend::toolY() const -> double { return m_toolPose.y; }
    auto RobotBackend::toolZ() const -> double { return m_toolPose.z; }
    auto RobotBackend::toolRollDegrees() const -> double { return radiansToDegrees(m_toolPose.roll); }
    auto RobotBackend::toolPitchDegrees() const -> double { return radiansToDegrees(m_toolPose.pitch); }
    auto RobotBackend::toolYawDegrees() const -> double { return radiansToDegrees(m_toolPose.yaw); }

    void RobotBackend::configureAds(core::link::ISymbolicLink* symbolicLink, AdsConfig config)
    {
        m_symbolicLink = symbolicLink;
        m_adsConfig = std::move(config);
        m_lastProcessedAdsJobId = 0;

        if (m_symbolicLink) {
            m_adsPollTimer->start();
            scheduleAdsStatusWrite();
        }
        else {
            m_adsPollTimer->stop();
        }
    }

    void RobotBackend::setConveyorBackend(ConveyorBackend* conveyorBackend)
    {
        m_conveyorBackend = conveyorBackend;
    }

    void RobotBackend::setRotaryTableBackend(RotaryTableBackend* rotaryTableBackend)
    {
        m_rotaryTableBackend = rotaryTableBackend;
    }

    void RobotBackend::detachSymbolicLink()
    {
        m_adsPollTimer->stop();
        m_adsPollInFlight = false;
        m_symbolicLink = nullptr;
    }

    void RobotBackend::executeJob(int jobId)
    {
        const auto poses = jobPoses(jobId);
        if (poses.empty()) {
            return;
        }

        m_currentTrajectory.clear();
        m_pendingActions.clear();
        m_trajectoryStep = 0;
        setManualMode(false);

        if (moveToPoseSequence(poses)) {
            setActiveJobId(jobId);
            setInMotion(true);
        }
    }

    void RobotBackend::jogJoint(int axisIndex, double deltaDegrees)
    {
        if (axisIndex < 0 || axisIndex >= static_cast<int>(m_jointAnglesDegrees.size())) {
            return;
        }

        std::array<double, 6> target = m_jointAnglesDegrees;
        target[static_cast<size_t>(axisIndex)] = normalizeJointToBounds(
          target[static_cast<size_t>(axisIndex)] + deltaDegrees, static_cast<size_t>(axisIndex));

        setManualMode(true);
        if (moveToJointTarget(target)) {
            setActiveJobId(0);
            setInMotion(true);
        }
    }

    bool RobotBackend::jogCartesian(double deltaX,
                                    double deltaY,
                                    double deltaZ,
                                    double deltaRollDegrees,
                                    double deltaPitchDegrees,
                                    double deltaYawDegrees)
    {
        auto target = m_toolPose;
        target.x += deltaX;
        target.y += deltaY;
        target.z += deltaZ;
        target.roll += degreesToRadians(deltaRollDegrees);
        target.pitch += degreesToRadians(deltaPitchDegrees);
        target.yaw += degreesToRadians(deltaYawDegrees);

        setManualMode(true);
        if (!moveToPoseSequence({ SequenceStep{ .kind = SequenceStep::Kind::Pose, .pose = target } })) {
            return false;
        }

        setActiveJobId(0);
        setInMotion(true);
        return true;
    }

    void RobotBackend::setManualMode(bool enabled)
    {
        if (m_manualMode == enabled) {
            return;
        }

        m_manualMode = enabled;
        emit manualModeChanged();
    }

    void RobotBackend::launchTask(QCoro::Task<void>&& task)
    {
        auto guarded = std::move(task).then([]() {}, [](const std::exception&) {});
        QCoro::connect(std::move(guarded), this, []() {});
    }

    void RobotBackend::startAdsPoll()
    {
        if (!m_symbolicLink || m_adsPollInFlight) {
            return;
        }

        m_adsPollInFlight = true;
        launchTask(pollAdsInputsAsync());
    }

    void RobotBackend::applyAdsInputs(int requestedJobId, const std::array<bool, 4>& plcAreas)
    {
        m_areaFreePlc = plcAreas;

        if (requestedJobId <= 0) {
            m_lastProcessedAdsJobId = 0;
            return;
        }

        if (m_manualMode || requestedJobId == m_lastProcessedAdsJobId || m_inMotion) {
            return;
        }

        m_lastProcessedAdsJobId = requestedJobId;
        executeJob(requestedJobId);
    }

    void RobotBackend::onSimulationTick()
    {
        const auto elapsedMs = static_cast<double>(m_stepClock.restart());
        const auto deltaSeconds = std::max(0.001, elapsedMs / 1000.0);

        if (!m_inMotion || m_currentTrajectory.empty()) {
            return;
        }

        if (m_trajectoryStep >= m_currentTrajectory.size()) {
            completeCurrentMotion();
            return;
        }

        const auto& waypoint = m_currentTrajectory[m_trajectoryStep];
        auto nextJoints = m_jointAnglesDegrees;
        const auto step = kJointSpeedDegreesPerSecond * deltaSeconds;
        bool waypointReached = true;

        for (size_t axis = 0; axis < nextJoints.size(); ++axis) {
            const auto diff = shortestWrappedDelta(nextJoints[axis], waypoint[axis], axis);
            if (std::abs(diff) > step) {
                nextJoints[axis] = normalizeJointToBounds(nextJoints[axis] + std::copysign(step, diff), axis);
                waypointReached = false;
            }
            else {
                nextJoints[axis] = waypoint[axis];
            }
        }

        setAxisValues(nextJoints);

        if (waypointReached) {
            ++m_trajectoryStep;
            processPendingActions();
        }

        if (m_trajectoryStep >= m_currentTrajectory.size()) {
            completeCurrentMotion();
        }
    }

    void RobotBackend::setAxisValues(const std::array<double, 6>& jointsDegrees)
    {
        const auto previous = m_jointAnglesDegrees;
        m_jointAnglesDegrees = jointsDegrees;

        if (!qFuzzyCompare(previous[0], m_jointAnglesDegrees[0])) {
            emit axis1Changed();
        }
        if (!qFuzzyCompare(previous[1], m_jointAnglesDegrees[1])) {
            emit axis2Changed();
        }
        if (!qFuzzyCompare(previous[2], m_jointAnglesDegrees[2])) {
            emit axis3Changed();
        }
        if (!qFuzzyCompare(previous[3], m_jointAnglesDegrees[3])) {
            emit axis4Changed();
        }
        if (!qFuzzyCompare(previous[4], m_jointAnglesDegrees[4])) {
            emit axis5Changed();
        }
        if (!qFuzzyCompare(previous[5], m_jointAnglesDegrees[5])) {
            emit axis6Changed();
        }

        updateToolPose();
        setAtHome(isHomeConfiguration(m_jointAnglesDegrees));
    }

    void RobotBackend::setGripperGripped(bool value)
    {
        if (m_gripperGripped == value) {
            return;
        }

        m_gripperGripped = value;
        emit gripperGrippedChanged();
        scheduleAdsStatusWrite();
    }

    void RobotBackend::setCarriedPartVisible(bool value)
    {
        if (m_carriedPartVisible == value) {
            return;
        }

        m_carriedPartVisible = value;
        emit carriedPartVisibleChanged();
    }

    void RobotBackend::setGripperSensorBlocked(bool value)
    {
        if (m_gripperSensorBlocked == value) {
            return;
        }

        m_gripperSensorBlocked = value;
        emit gripperSensorBlockedChanged();
    }

    void RobotBackend::setActiveJobId(int value)
    {
        if (m_activeJobId == value) {
            return;
        }

        m_activeJobId = value;
        emit activeJobIdChanged();
        scheduleAdsStatusWrite();
    }

    void RobotBackend::setInMotion(bool value)
    {
        if (m_inMotion == value) {
            return;
        }

        m_inMotion = value;
        emit inMotionChanged();
        updateRobotAreaState();
    }

    void RobotBackend::setAtHome(bool value)
    {
        if (m_atHome == value) {
            return;
        }

        m_atHome = value;
        emit atHomeChanged();
    }

    void RobotBackend::updateToolPose()
    {
        m_toolPose = m_kinematics.forward(jointsToRadians(m_jointAnglesDegrees));
        emit toolPoseChanged();
    }

    void RobotBackend::updateRobotAreaState()
    {
        const auto nextValue = !m_inMotion;
        if (m_areaFreeRobot == std::array<bool, 4>{ nextValue, nextValue, nextValue, nextValue }) {
            scheduleAdsStatusWrite();
            return;
        }

        m_areaFreeRobot = { nextValue, nextValue, nextValue, nextValue };
        scheduleAdsStatusWrite();
    }

    void RobotBackend::scheduleAdsStatusWrite()
    {
        if (!m_symbolicLink) {
            return;
        }

        launchTask(writeStatusAsync(m_activeJobId, m_areaFreeRobot));
    }

    void RobotBackend::applySequenceAction(SequenceAction action)
    {
        switch (action) {
            case SequenceAction::Pick: {
                const auto pickedPart = !m_gripperGripped && m_rotaryTableBackend &&
                                        m_rotaryTableBackend->tryTakePartForRobotPick();
                if (pickedPart) {
                    setGripperSensorBlocked(true);
                    setGripperGripped(true);
                    setCarriedPartVisible(true);
                }
                break;
            }
            case SequenceAction::Place:
                if (m_gripperGripped && m_conveyorBackend && m_conveyorBackend->tryPlacePartFromRobot()) {
                    setGripperGripped(false);
                    setCarriedPartVisible(false);
                    setGripperSensorBlocked(false);
                }
                break;
        }
    }

    void RobotBackend::processPendingActions()
    {
        while (!m_pendingActions.empty() && m_pendingActions.front().trajectoryIndex == m_trajectoryStep) {
            applySequenceAction(m_pendingActions.front().action);
            m_pendingActions.erase(m_pendingActions.begin());
        }
    }

    void RobotBackend::completeCurrentMotion()
    {
        m_currentTrajectory.clear();
        m_pendingActions.clear();
        m_trajectoryStep = 0;
        setInMotion(false);

        setActiveJobId(0);
    }

    auto RobotBackend::moveToPoseSequence(const std::vector<SequenceStep>& steps) -> bool
    {
        if (steps.empty()) {
            return false;
        }

        auto current = m_jointAnglesDegrees;
        auto currentSeed = jointsToRadians(current);
        std::vector<std::array<double, 6>> trajectory;
        std::vector<PendingAction> pendingActions;

        for (const auto& step : steps) {
            if (step.kind == SequenceStep::Kind::Action) {
                pendingActions.push_back(
                  PendingAction{ .trajectoryIndex = trajectory.size(), .action = step.action });
                continue;
            }

            const auto jointsRadians = m_kinematics.inverse(step.pose, currentSeed);
            if (jointsRadians.empty()) {
                return false;
            }

            std::array<double, 6> targetDegrees{};
            std::array<double, 6> targetSeed{};
            for (size_t axis = 0; axis < targetDegrees.size(); ++axis) {
                targetDegrees[axis] = normalizeJointToBounds(radiansToDegrees(jointsRadians[axis]), axis);
                targetSeed[axis] = jointsRadians[axis];
            }

            auto segment = planTrajectory(current, targetDegrees);
            if (!trajectory.empty() && !segment.empty()) {
                segment.erase(segment.begin());
            }
            trajectory.insert(trajectory.end(), segment.begin(), segment.end());

            current = targetDegrees;
            currentSeed = targetSeed;
        }

        if (trajectory.empty()) {
            return false;
        }

        m_currentTrajectory = std::move(trajectory);
        m_pendingActions = std::move(pendingActions);
        m_trajectoryStep = 0;
        processPendingActions();
        return true;
    }

    auto RobotBackend::moveToJointTarget(const std::array<double, 6>& jointsDegrees) -> bool
    {
        auto trajectory = planTrajectory(m_jointAnglesDegrees, jointsDegrees);
        if (trajectory.empty()) {
            return false;
        }

        m_currentTrajectory = std::move(trajectory);
        m_trajectoryStep = 0;
        return true;
    }

    auto RobotBackend::planTrajectory(const std::array<double, 6>& startJoints,
                                      const std::array<double, 6>& targetJoints) const
      -> std::vector<std::array<double, 6>>
    {
        std::array<double, 6> normalizedStart{};
        std::array<double, 6> deltas{};
        double maxAbsDelta = 0.0;

        for (size_t axis = 0; axis < normalizedStart.size(); ++axis) {
            normalizedStart[axis] = normalizeJointToBounds(startJoints[axis], axis);
            const auto normalizedTarget = normalizeJointToBounds(targetJoints[axis], axis);
            deltas[axis] = shortestWrappedDelta(normalizedStart[axis], normalizedTarget, axis);
            maxAbsDelta = std::max(maxAbsDelta, std::abs(deltas[axis]));
        }

        const auto segmentCount = std::max(1, static_cast<int>(std::ceil(maxAbsDelta / 8.0)));

        std::vector<std::array<double, 6>> trajectory;
        trajectory.reserve(static_cast<size_t>(segmentCount + 1));

        for (int segment = 0; segment <= segmentCount; ++segment) {
            const auto t = static_cast<double>(segment) / static_cast<double>(segmentCount);
            std::array<double, 6> waypoint{};
            for (size_t axis = 0; axis < waypoint.size(); ++axis) {
                waypoint[axis] = normalizeJointToBounds(normalizedStart[axis] + deltas[axis] * t, axis);
            }
            trajectory.push_back(waypoint);
        }

        return trajectory;
    }

    auto RobotBackend::writeStatusAsync(int activeJobId, std::array<bool, 4> areaFreeRobot)
      -> QCoro::Task<void>
    {
        if (!m_symbolicLink) {
            co_return;
        }

        if (!m_adsConfig.actualJobIdVariable.trimmed().isEmpty()) {
            switch (m_adsConfig.actualJobIdType) {
                case JobIdType::Int8: {
                    auto writeResult = co_await toQCoroTask(m_symbolicLink->write(
                      m_adsConfig.actualJobIdVariable.toStdString(), static_cast<int8_t>(activeJobId)));
                    (void)writeResult;
                    break;
                }
                case JobIdType::UInt8: {
                    auto writeResult = co_await toQCoroTask(
                      m_symbolicLink->write(m_adsConfig.actualJobIdVariable.toStdString(),
                                            static_cast<uint8_t>(std::max(0, activeJobId))));
                    (void)writeResult;
                    break;
                }
                case JobIdType::Int16: {
                    auto writeResult = co_await toQCoroTask(m_symbolicLink->write(
                      m_adsConfig.actualJobIdVariable.toStdString(), static_cast<int16_t>(activeJobId)));
                    (void)writeResult;
                    break;
                }
                case JobIdType::UInt16: {
                    auto writeResult = co_await toQCoroTask(
                      m_symbolicLink->write(m_adsConfig.actualJobIdVariable.toStdString(),
                                            static_cast<uint16_t>(std::max(0, activeJobId))));
                    (void)writeResult;
                    break;
                }
                case JobIdType::UInt32: {
                    auto writeResult = co_await toQCoroTask(
                      m_symbolicLink->write(m_adsConfig.actualJobIdVariable.toStdString(),
                                            static_cast<uint32_t>(std::max(0, activeJobId))));
                    (void)writeResult;
                    break;
                }
                case JobIdType::Int32: {
                    auto writeResult = co_await toQCoroTask(m_symbolicLink->write(
                      m_adsConfig.actualJobIdVariable.toStdString(), static_cast<int32_t>(activeJobId)));
                    (void)writeResult;
                    break;
                }
            }
        }

        if (!m_adsConfig.gripperSensorVariable.trimmed().isEmpty()) {
            auto writeResult = co_await toQCoroTask(
              m_symbolicLink->write(m_adsConfig.gripperSensorVariable.toStdString(), m_gripperGripped));
            (void)writeResult;
        }

        for (size_t index = 0; index < m_adsConfig.areaFreeRobotVariables.size(); ++index) {
            const auto& variableName = m_adsConfig.areaFreeRobotVariables[index];
            if (variableName.trimmed().isEmpty()) {
                continue;
            }

            auto writeResult =
              co_await toQCoroTask(m_symbolicLink->write(variableName.toStdString(), areaFreeRobot[index]));
            (void)writeResult;
        }

        co_return;
    }

    auto RobotBackend::pollAdsInputsAsync() -> QCoro::Task<void>
    {
        int requestedJobId = 0;
        std::array<bool, 4> plcAreas = m_areaFreePlc;

        if (m_symbolicLink && !m_adsConfig.jobIdVariable.trimmed().isEmpty()) {
            switch (m_adsConfig.jobIdType) {
                case JobIdType::Int8: {
                    const auto result = co_await toQCoroTask(
                      m_symbolicLink->read<int8_t>(m_adsConfig.jobIdVariable.toStdString()));
                    if (result) {
                        requestedJobId = static_cast<int>(result.value());
                    }
                    break;
                }
                case JobIdType::UInt8: {
                    const auto result = co_await toQCoroTask(
                      m_symbolicLink->read<uint8_t>(m_adsConfig.jobIdVariable.toStdString()));
                    if (result) {
                        requestedJobId = static_cast<int>(result.value());
                    }
                    break;
                }
                case JobIdType::Int16: {
                    const auto result = co_await toQCoroTask(
                      m_symbolicLink->read<int16_t>(m_adsConfig.jobIdVariable.toStdString()));
                    if (result) {
                        requestedJobId = static_cast<int>(result.value());
                    }
                    break;
                }
                case JobIdType::UInt16: {
                    const auto result = co_await toQCoroTask(
                      m_symbolicLink->read<uint16_t>(m_adsConfig.jobIdVariable.toStdString()));
                    if (result) {
                        requestedJobId = static_cast<int>(result.value());
                    }
                    break;
                }
                case JobIdType::UInt32: {
                    const auto result = co_await toQCoroTask(
                      m_symbolicLink->read<uint32_t>(m_adsConfig.jobIdVariable.toStdString()));
                    if (result) {
                        requestedJobId = static_cast<int>(result.value());
                    }
                    break;
                }
                case JobIdType::Int32: {
                    const auto result = co_await toQCoroTask(
                      m_symbolicLink->read<int32_t>(m_adsConfig.jobIdVariable.toStdString()));
                    if (result) {
                        requestedJobId = static_cast<int>(result.value());
                    }
                    break;
                }
            }
        }

        for (size_t index = 0; index < m_adsConfig.areaFreePlcVariables.size(); ++index) {
            const auto& variableName = m_adsConfig.areaFreePlcVariables[index];
            if (variableName.trimmed().isEmpty()) {
                continue;
            }

            const auto result = co_await toQCoroTask(m_symbolicLink->read<bool>(variableName.toStdString()));
            if (result) {
                plcAreas[index] = result.value();
            }
        }

        QMetaObject::invokeMethod(this, [this, requestedJobId, plcAreas]() {
            m_adsPollInFlight = false;
            applyAdsInputs(requestedJobId, plcAreas);
        }, Qt::QueuedConnection);

        co_return;
    }
}