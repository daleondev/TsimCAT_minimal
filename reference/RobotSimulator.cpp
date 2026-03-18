#include "RobotSimulator.hpp"
#include "Coroutines/Task.hpp"
#include "Link/Symbolic/ISymbolicLink.hpp"
#include "Link/Symbolic/LocalAdsLink.hpp"
#include "Logger/Logger.hpp"
#include "Logger/TraceLogger.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace core::sim
{
    namespace
    {
        constexpr std::array<double, 6> kJointLowerBounds = {
            -180.0, -150.0, -150.0, -180.0, -125.0, -180.0
        };
        constexpr std::array<double, 6> kJointUpperBounds = { 180.0, 150.0, 150.0, 180.0, 125.0, 180.0 };
        constexpr std::array<bool, 6> kWrapJoints = { true, false, false, true, false, true };

        auto normalizeJointToBounds(double value, size_t axis) -> double
        {
            const double low = kJointLowerBounds[axis];
            const double high = kJointUpperBounds[axis];

            while (value < low) {
                value += 360.0;
            }
            while (value > high) {
                value -= 360.0;
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
    }

    RobotSimulator::RobotSimulator(std::shared_ptr<link::ILink> link)
      : RobotSimulator(std::move(link), AdsSymbols{}, Config{})
    {
    }

    RobotSimulator::RobotSimulator(std::shared_ptr<link::ILink> link, Config config)
      : RobotSimulator(std::move(link), AdsSymbols{}, std::move(config))
    {
    }

    RobotSimulator::RobotSimulator(std::shared_ptr<link::ILink> link, AdsSymbols adsSymbols, Config config)
      : m_link(std::move(link))
      , m_adsSymbols(std::move(adsSymbols))
    {
        auto trajectories = defaultJobTrajectories();
        for (auto& configuredTrajectory : config.jobTrajectories) {
            if (configuredTrajectory.jobId == 0 || configuredTrajectory.poses.empty()) {
                continue;
            }

            const auto existing =
              std::find_if(trajectories.begin(), trajectories.end(), [&](const auto& trajectory) {
                return trajectory.jobId == configuredTrajectory.jobId;
            });
            if (existing != trajectories.end()) {
                existing->poses = std::move(configuredTrajectory.poses);
            }
            else {
                trajectories.push_back(std::move(configuredTrajectory));
            }
        }

        for (auto& trajectory : trajectories) {
            if (trajectory.jobId == 0 || trajectory.poses.empty()) {
                continue;
            }
            m_jobTrajectories.emplace(trajectory.jobId, std::move(trajectory.poses));
            logger::info("RobotSimulator: Loaded trajectory for Job {} with {} poses",
                         trajectory.jobId,
                         m_jobTrajectories[trajectory.jobId].size());
        }

        // Initial hardware-like state
        m_status.bInHome = 1;
        m_status.bEnabled = 1;
        m_status.bInAut = 1;
        m_status.bMasteringOk = 1;
        m_status.bBrakeTestOk = 1;

        // Initialize to a nice "Home" position (Degrees)
        m_jointAngles[0] = 0.0;   // Axis 1
        m_jointAngles[1] = -90.0; // Axis 2 (Upright)
        m_jointAngles[2] = 90.0;  // Axis 3 (Horizontal)
        m_jointAngles[3] = 0.0;   // Axis 4
        m_jointAngles[4] = 0.0;   // Axis 5
        m_jointAngles[5] = 0.0;   // Axis 6

        for (int i = 0; i < 6; ++i)
            m_targetJointAngles[i] = m_jointAngles[i];

        // Test Kinematics
        std::array<double, 6> rads;
        for (int i = 0; i < 6; ++i)
            rads[i] = m_jointAngles[i] * std::numbers::pi / 180.0;
        auto pose = m_kinematics.forward(rads);
        logger::info(
          "RobotSimulator: Initial Pose [X: {:.3f}, Y: {:.3f}, Z: {:.3f}]", pose.x, pose.y, pose.z);
    }

    RobotSimulator::~RobotSimulator() { stop(); }

    auto RobotSimulator::initialize() -> coro::Task<result::Result<void>>
    {
        if (!m_link || m_internalMode) {
            logger::TraceLogger::instance().emit(
              logger::TraceCategory::Lifecycle,
              "robot",
              "initialize_skipped",
              { logger::traceField("reason", !m_link ? "no_link" : "internal_mode") });
            co_return result::success();
        }

        if (auto* client = m_link->asClient()) {
            logger::info("RobotSimulator: Connecting to ADS...");
            auto res = co_await client->connect();
            if (!res) {
                logger::error("RobotSimulator: ADS Connection failed: {}", res.error().message());
                logger::TraceLogger::instance().emit(logger::TraceCategory::Protocol,
                                                     "robot",
                                                     "ads_connect_failed",
                                                     { logger::traceField("error", res.error().message()) });
                co_return std::unexpected(res.error());
            }
            else {
                logger::info("RobotSimulator: ADS Connected");
                logger::TraceLogger::instance().emit(logger::TraceCategory::Protocol,
                                                     "robot",
                                                     "ads_connected",
                                                     { logger::traceField("status", "connected") });
            }
        }
        co_return result::success();
    }

    auto RobotSimulator::start() -> void { m_running = true; }

    auto RobotSimulator::stop() -> void { m_running = false; }

    auto RobotSimulator::update(double deltaTimeSeconds) -> void
    {
        if (!m_internalMode && m_externalCommandSimulationEnabled) {
            if (auto* localAds = dynamic_cast<link::symbolic::LocalAdsLink*>(m_link.get())) {
                const auto control = localAds->readSync<RobotControl>(m_adsSymbols.controlSymbol);
                std::scoped_lock controlLock(m_mutex);
                m_control = control;
            }
        }

        // Logic Simulation
        std::scoped_lock lock(m_mutex);

        const bool autoCommandActive =
          m_externalCommandSimulationEnabled && m_control.bMoveEnable && !m_status.bError;
        const bool motionRequested = m_manualTrajectoryActive || autoCommandActive;

        if (motionRequested) {
            // 1. Check if Job ID changed -> Plan New Trajectory
            if (autoCommandActive && m_control.nJobId != m_lastTargetJobId) {
                const auto requestedJobId = m_control.nJobId;

                const auto* poses = configuredPosesForJob(requestedJobId);

                if (poses && !poses->empty()) {
                    std::array<double, 6> startJoints;
                    for (int i = 0; i < 6; ++i)
                        startJoints[i] = m_jointAngles[i];

                    std::vector<std::array<double, 6>> trajectory;
                    if (planTrajectoryThroughPoses(startJoints, *poses, trajectory)) {
                        m_currentTrajectory = std::move(trajectory);
                        m_trajectoryStep = 0;
                        m_lastTargetJobId = requestedJobId;
                        m_lastSuccessfulJobId = requestedJobId;
                        m_status.nJobIdFeedback = requestedJobId;
                        m_status.bError = 0;
                        m_status.nErrorCode = 0;

                        logger::info(
                          "RobotSimulator: Planned blended trajectory for Job {} with {} samples across {} "
                          "configured poses",
                          requestedJobId,
                          m_currentTrajectory.size(),
                          poses->size());
                        logger::TraceLogger::instance().emit(
                          logger::TraceCategory::State,
                          "robot",
                          "trajectory_planned",
                          { logger::traceField("job_id", static_cast<int>(requestedJobId)),
                            logger::traceField("samples", static_cast<int>(m_currentTrajectory.size())),
                            logger::traceField("poses", static_cast<int>(poses->size())) });
                    }
                    else {
                        m_currentTrajectory.clear();
                        m_trajectoryStep = 0;
                        logger::error("RobotSimulator: Failed to plan multi-pose trajectory for Job {}",
                                      requestedJobId);
                        logger::TraceLogger::instance().emit(
                          logger::TraceCategory::Invariant,
                          "robot",
                          "multi_pose_plan_failed",
                          { logger::traceField("job_id", static_cast<int>(requestedJobId)),
                            logger::traceField("pose_count", static_cast<int>(poses->size())) });
                    }
                }
                else {
                    m_currentTrajectory.clear();
                    m_trajectoryStep = 0;
                    logger::error("RobotSimulator: No configured trajectory for Job {}", requestedJobId);
                    logger::TraceLogger::instance().emit(
                      logger::TraceCategory::Invariant,
                      "robot",
                      "job_trajectory_missing",
                      { logger::traceField("job_id", static_cast<int>(requestedJobId)) });
                }
            }

            // 2. Follow Trajectory
            if (!m_currentTrajectory.empty() && m_trajectoryStep < m_currentTrajectory.size()) {
                const auto& target = m_currentTrajectory[m_trajectoryStep];

                // Interpolate towards the current waypoint
                const double jointSpeedDegreesPerSecond = 150.0;
                const double step = jointSpeedDegreesPerSecond * deltaTimeSeconds;
                bool waypointReached = true;

                for (int i = 0; i < 6; ++i) {
                    double diff = target[i] - m_jointAngles[i];
                    if (std::abs(diff) > step) {
                        m_jointAngles[i] += std::copysign(step, diff);
                        waypointReached = false;
                    }
                    else {
                        m_jointAngles[i] = target[i];
                    }
                }

                if (waypointReached) {
                    m_trajectoryStep++;
                }

                m_status.bInMotion = 1;
                m_status.bInHome = 0;
            }
            else {
                m_status.bInMotion = 0;

                if (m_trajectoryStep >= m_currentTrajectory.size() && !m_currentTrajectory.empty()) {
                    if (!m_manualTrajectoryActive) {
                        applyJobCompletionEffects(m_control.nJobId);
                    }
                    m_manualTrajectoryActive = false;
                    m_currentTrajectory.clear();
                    m_trajectoryStep = 0;
                }

                // Check if we are at home position waypoints
                std::array<double, 6> homeJoints = { 0.0, -90.0, 90.0, 0.0, 0.0, 0.0 };
                bool atHome = true;
                for (int i = 0; i < 6; ++i) {
                    if (std::abs(m_jointAngles[i] - homeJoints[i]) > 0.1) {
                        atHome = false;
                        break;
                    }
                }
                m_status.bInHome = atHome ? 1 : 0;
            }
        }
        else {
            m_status.bInMotion = 0;
        }

        if (!m_internalMode) {
            if (auto* localAds = dynamic_cast<link::symbolic::LocalAdsLink*>(m_link.get())) {
                localAds->writeSync(m_adsSymbols.statusSymbol, m_status);
                if (!m_adsSymbols.gripperSensorSymbol.empty()) {
                    localAds->writeSync(m_adsSymbols.gripperSensorSymbol, m_gripperSensorBlocked);
                }
            }
        }
    }

    auto RobotSimulator::defaultJobTrajectories() -> std::vector<JobTrajectory>
    {
        using std::numbers::pi;

        return {
            JobTrajectory{ .jobId = static_cast<uint16_t>(JobId::Home),
                           .poses = { Pose{ 395.0, 0.0, 765.0, 0.0, 90.0 * pi / 180.0, 0.0 } } },
            JobTrajectory{ .jobId = static_cast<uint16_t>(JobId::PickEntry),
                           .poses = { Pose{ 395.0, 0.0, 765.0, 0.0, 90.0 * pi / 180.0, 0.0 },
                                      Pose{ 615.0, 650.0, 520.0, 0.0, 0.0, 90.0 * pi / 180.0 },
                                      Pose{ 395.0, 0.0, 765.0, 0.0, 90.0 * pi / 180.0, 0.0 } } },
            JobTrajectory{ .jobId = static_cast<uint16_t>(JobId::PlaceLaser),
                           .poses = { Pose{ 395.0, 0.0, 765.0, 0.0, 90.0 * pi / 180.0, 0.0 },
                                      Pose{ 520.0, 0.0, 760.0, 0.0, 0.0, 0.0 },
                                      Pose{ 670.0, 0.0, 630.0, 0.0, 0.0, 0.0 },
                                      Pose{ 520.0, 0.0, 760.0, 0.0, 0.0, 0.0 },
                                      Pose{ 395.0, 0.0, 765.0, 0.0, 90.0 * pi / 180.0, 0.0 } } },
            JobTrajectory{ .jobId = static_cast<uint16_t>(JobId::PickLaser),
                           .poses = { Pose{ 520.0, 0.0, 760.0, 0.0, 0.0, 0.0 },
                                      Pose{ 670.0, 0.0, 630.0, 0.0, 0.0, 0.0 },
                                      Pose{ 670.0, 0.0, 565.0, 0.0, 0.0, 0.0 },
                                      Pose{ 670.0, 0.0, 630.0, 0.0, 0.0, 0.0 },
                                      Pose{ 520.0, 0.0, 760.0, 0.0, 0.0, 0.0 } } },
            JobTrajectory{ .jobId = static_cast<uint16_t>(JobId::PlaceExit),
                           .poses = { Pose{ 395.0, 0.0, 765.0, 0.0, 90.0 * pi / 180.0, 0.0 },
                                      Pose{ 615.0, -690.0, 520.0, 0.0, 0.0, -90.0 * pi / 180.0 },
                                      Pose{ 395.0, 0.0, 765.0, 0.0, 90.0 * pi / 180.0, 0.0 } } },
        };
    }

    auto RobotSimulator::configuredPosesForJob(uint16_t jobId) const -> const std::vector<Pose>*
    {
        if (const auto it = m_jobTrajectories.find(jobId); it != m_jobTrajectories.end()) {
            return &it->second;
        }

        logger::TraceLogger::instance().emit(logger::TraceCategory::Invariant,
                                             "robot",
                                             "unknown_job_id",
                                             { logger::traceField("job_id", static_cast<int>(jobId)) });
        return nullptr;
    }

    auto RobotSimulator::planTrajectoryThroughPoses(const std::array<double, 6>& startJoints,
                                                    const std::vector<Pose>& poses,
                                                    std::vector<std::array<double, 6>>& outTrajectory) const
      -> bool
    {
        if (poses.empty()) {
            outTrajectory.clear();
            return false;
        }

        using std::numbers::pi;

        std::array<double, 6> currentJoints = startJoints;
        std::array<double, 6> currentSeedRadians{};
        for (int i = 0; i < 6; ++i) {
            currentSeedRadians[i] = currentJoints[i] * pi / 180.0;
        }

        outTrajectory.clear();

        for (const auto& pose : poses) {
            const auto targetRadsVec = m_kinematics.inverse(pose, currentSeedRadians);
            if (targetRadsVec.empty()) {
                outTrajectory.clear();
                return false;
            }

            std::array<double, 6> targetJoints{};
            std::array<double, 6> targetSeedRadians{};
            for (int i = 0; i < 6; ++i) {
                targetSeedRadians[i] = targetRadsVec[i];
                targetJoints[i] = targetRadsVec[i] * 180.0 / pi;
            }

            auto segment = planTrajectory(currentJoints, targetJoints);
            if (!outTrajectory.empty() && !segment.empty()) {
                segment.erase(segment.begin());
            }
            outTrajectory.insert(outTrajectory.end(), segment.begin(), segment.end());

            currentJoints = targetJoints;
            currentSeedRadians = targetSeedRadians;
        }

        return !outTrajectory.empty();
    }

    auto RobotSimulator::applyJobCompletionEffects(uint16_t jobId) -> void
    {
        switch (static_cast<JobId>(jobId)) {
            case JobId::PickEntry:
            case JobId::PickLaser:
                if (m_gripperSensorBlocked) {
                    m_gripperGripped = true;
                }
                break;
            case JobId::PlaceLaser:
            case JobId::PlaceExit:
                m_gripperGripped = false;
                m_gripperSensorBlocked = false;
                break;
            case JobId::Home:
            default:
                break;
        }
    }

    auto RobotSimulator::planTrajectory(const std::array<double, 6>& startJoints,
                                        const std::array<double, 6>& targetJoints) const
      -> std::vector<std::array<double, 6>>
    {
        std::array<double, 6> normalizedStart{};
        std::array<double, 6> deltas{};
        double maxAbsDelta = 0.0;

        for (size_t axis = 0; axis < normalizedStart.size(); ++axis) {
            normalizedStart[axis] = normalizeJointToBounds(startJoints[axis], axis);
            const double normalizedTarget = normalizeJointToBounds(targetJoints[axis], axis);
            deltas[axis] = shortestWrappedDelta(normalizedStart[axis], normalizedTarget, axis);
            maxAbsDelta = std::max(maxAbsDelta, std::abs(deltas[axis]));
        }

        const int segmentCount = std::max(1, static_cast<int>(std::ceil(maxAbsDelta / 8.0)));

        std::vector<std::array<double, 6>> result;
        result.reserve(static_cast<size_t>(segmentCount + 1));

        for (int segment = 0; segment <= segmentCount; ++segment) {
            const double t = static_cast<double>(segment) / static_cast<double>(segmentCount);
            std::array<double, 6> waypoint{};
            for (size_t axis = 0; axis < waypoint.size(); ++axis) {
                waypoint[axis] = normalizeJointToBounds(normalizedStart[axis] + deltas[axis] * t, axis);
            }
            result.push_back(waypoint);
        }

        return result;
    }

    auto RobotSimulator::control() const -> RobotControl
    {
        std::scoped_lock lock(m_mutex);
        return m_control;
    }

    auto RobotSimulator::status() const -> RobotStatus
    {
        std::scoped_lock lock(m_mutex);
        return m_status;
    }

    auto RobotSimulator::adsStatus() const -> std::string
    {
        if (m_internalMode) {
            return "Local";
        }
        if (!m_link)
            return "No Link";
        switch (m_link->status()) {
            case link::Status::Disconnected:
                return "Disconnected";
            case link::Status::Connecting:
                return "Connecting";
            case link::Status::Connected:
                return "Connected";
            case link::Status::Faulty:
                return "Faulty";
            default:
                return "Unknown";
        }
    }

    auto RobotSimulator::jointAngles() const -> const double*
    {
        std::scoped_lock lock(m_mutex);
        return m_jointAngles;
    }

    auto RobotSimulator::run() -> coro::Task<void>
    {
        if (m_internalMode || !m_link)
            co_return;

        if (dynamic_cast<link::symbolic::LocalAdsLink*>(m_link.get())) {
            co_return;
        }

        auto* symbolic = m_link->asSymbolic();
        if (!symbolic)
            co_return;

        while (m_running) {
            if (m_internalMode) {
                co_await coro::sleep(std::chrono::milliseconds(100));
                continue;
            }

            // Only communicate if actually connected to avoid floods
            if (m_link->status() == link::Status::Connected) {
                // 1. Read Commands
                auto ctrlRes = co_await symbolic->read<RobotControl>(m_adsSymbols.controlSymbol);
                if (ctrlRes) {
                    std::scoped_lock lock(m_mutex);
                    m_control = *ctrlRes;
                    logger::TraceLogger::instance().emit(
                      logger::TraceCategory::Protocol,
                      "robot",
                      "ads_rx_control",
                      { logger::traceField("job_id", static_cast<int>(m_control.nJobId)),
                        logger::traceField("move_enable", m_control.bMoveEnable != 0),
                        logger::traceField("symbol", m_adsSymbols.controlSymbol) });
                }

                // 2. Write Status
                RobotStatus s;
                {
                    std::scoped_lock lock(m_mutex);
                    s = m_status;
                }
                (void)co_await symbolic->write(m_adsSymbols.statusSymbol, s);
                logger::TraceLogger::instance().emit(
                  logger::TraceCategory::Protocol,
                  "robot",
                  "ads_tx_status",
                  { logger::traceField("job_feedback", static_cast<int>(s.nJobIdFeedback)),
                    logger::traceField("in_motion", s.bInMotion != 0),
                    logger::traceField("error", s.bError != 0),
                    logger::traceField("symbol", m_adsSymbols.statusSymbol) });
            }
            else {
                // Throttled wait when disconnected
                logger::TraceLogger::instance().emit(
                  logger::TraceCategory::Lifecycle,
                  "robot",
                  "ads_disconnected_wait",
                  { logger::traceField("status", static_cast<int>(m_link->status())) });
                co_await coro::sleep(std::chrono::milliseconds(500));
            }

            // Standard loop throttle
            co_await coro::sleep(std::chrono::milliseconds(50));
        }
    }

    auto RobotSimulator::currentPose() const -> Pose
    {
        std::scoped_lock lock(m_mutex);
        std::array<double, 6> rads;
        for (int i = 0; i < 6; ++i)
            rads[i] = m_jointAngles[i] * std::numbers::pi / 180.0;
        return m_kinematics.forward(rads);
    }

    auto RobotSimulator::isGripperGripped() const -> bool
    {
        std::scoped_lock lock(m_mutex);
        return m_gripperGripped;
    }

    auto RobotSimulator::setGripper(bool gripped) -> void
    {
        std::scoped_lock lock(m_mutex);
        m_gripperGripped = gripped;
    }

    auto RobotSimulator::isGripperSensorBlocked() const -> bool
    {
        std::scoped_lock lock(m_mutex);
        return m_gripperSensorBlocked;
    }

    auto RobotSimulator::setGripperSensorBlocked(bool blocked) -> void
    {
        std::scoped_lock lock(m_mutex);
        m_gripperSensorBlocked = blocked;
    }

    auto RobotSimulator::setJointAngles(const double* anglesDegrees) -> void
    {
        std::scoped_lock lock(m_mutex);
        for (int i = 0; i < 6; ++i) {
            m_jointAngles[i] = anglesDegrees[i];
        }
        m_currentTrajectory.clear();
        m_manualTrajectoryActive = false;
    }

    auto RobotSimulator::setTargetPose(const Pose& pose) -> bool
    {
        std::array<double, 6> seed;
        {
            std::scoped_lock lock(m_mutex);
            for (int i = 0; i < 6; ++i)
                seed[i] = m_jointAngles[i] * std::numbers::pi / 180.0;
        }

        auto joints = m_kinematics.inverse(pose, seed);
        if (joints.empty()) {
            return false;
        }

        std::array<double, 6> start;
        std::array<double, 6> target;
        {
            std::scoped_lock lock(m_mutex);
            for (int i = 0; i < 6; ++i) {
                start[i] = m_jointAngles[i];
                target[i] = joints[i] * 180.0 / std::numbers::pi;
            }
        }

        auto path = planTrajectory(start, target);
        std::scoped_lock lock(m_mutex);
        m_currentTrajectory = std::move(path);
        m_trajectoryStep = 0;
        m_manualTrajectoryActive = true;
        return true;
    }

    auto RobotSimulator::setExternalCommandSimulationEnabled(bool enabled) -> void
    {
        std::scoped_lock lock(m_mutex);
        m_externalCommandSimulationEnabled = enabled;
        if (!enabled) {
            m_control.bMoveEnable = 0;
            m_control.nJobId = 0;
            m_lastTargetJobId = 0;
        }
    }

    auto RobotSimulator::externalCommandSimulationEnabled() const -> bool
    {
        std::scoped_lock lock(m_mutex);
        return m_externalCommandSimulationEnabled;
    }

    auto RobotSimulator::triggerJob(uint16_t jobId) -> void
    {
        if (auto* localAds = dynamic_cast<link::symbolic::LocalAdsLink*>(m_link.get())) {
            auto control = localAds->readSync<RobotControl>(m_adsSymbols.controlSymbol);
            control.nJobId = jobId;
            control.bMoveEnable = 1;
            localAds->writeSync(m_adsSymbols.controlSymbol, control);
            return;
        }

        std::scoped_lock lock(m_mutex);
        if (!m_internalMode) {
            logger::TraceLogger::instance().emit(logger::TraceCategory::Lifecycle,
                                                 "robot",
                                                 "job_trigger_ignored_remote_mode",
                                                 { logger::traceField("job_id", static_cast<int>(jobId)) });
            return;
        }
        m_control.nJobId = jobId;
        m_control.bMoveEnable = 1; // Auto-enable move for convenience in simulation
        logger::TraceLogger::instance().emit(logger::TraceCategory::Flow,
                                             "robot",
                                             "job_triggered",
                                             { logger::traceField("job_id", static_cast<int>(jobId)) });
    }

    auto RobotSimulator::setAdsLink(std::shared_ptr<link::ILink> link) -> void
    {
        std::scoped_lock lock(m_mutex);
        m_link = std::move(link);
    }

    auto RobotSimulator::setInternalMode(bool internalMode) -> void
    {
        std::scoped_lock lock(m_mutex);
        if (m_internalMode == internalMode) {
            return;
        }

        m_internalMode = internalMode;
        if (!m_internalMode) {
            m_control.nJobId = 0;
            m_control.bMoveEnable = 0;
            m_currentTrajectory.clear();
            m_trajectoryStep = 0;
            m_lastTargetJobId = 0;
            m_lastSuccessfulJobId = 0;
            m_status.bInMotion = 0;
            m_status.nJobIdFeedback = 0;
            logger::TraceLogger::instance().emit(logger::TraceCategory::State,
                                                 "robot",
                                                 "local_motion_quiesced",
                                                 { logger::traceField("reason", "switched_to_remote_mode") });
        }
        logger::TraceLogger::instance().emit(logger::TraceCategory::Lifecycle,
                                             "robot",
                                             "internal_mode_changed",
                                             { logger::traceField("internal", internalMode) });
    }

    auto RobotSimulator::isInternalMode() const -> bool
    {
        std::scoped_lock lock(m_mutex);
        return m_internalMode;
    }
}