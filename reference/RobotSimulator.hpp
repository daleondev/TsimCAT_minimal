#pragma once

#include "ISimulator.hpp"
#include "Kinematics.hpp"
#include "Link/ILink.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace core::sim
{
/**
 * Commands sent from the PLC to the Robot.
 * Uses bitfields to map to PLC BIT types (packed).
 */
#pragma pack(push, 1)
    struct RobotControl
    {
        uint16_t nJobId; // Job number to execute

        // Control Bits (1 byte total)
        uint8_t bMoveEnable : 1; // Bit 0: Permission to move
        uint8_t bReset : 1;      // Bit 1: Reset robot errors
        uint8_t reserved : 6;

        uint8_t nAreaFree_PLC; // Bitmask: PLC signals if Area [0..7] is free for robot
    };

    /**
     * Status reported from the Robot simulation back to the PLC.
     */
    struct RobotStatus
    {
        uint16_t nJobIdFeedback; // Currently active / last completed job

        // Status Bits (1 byte total)
        uint8_t bInMotion : 1;    // Bit 0: Robot is currently moving
        uint8_t bInHome : 1;      // Bit 1: Robot is in safe home position
        uint8_t bEnabled : 1;     // Bit 2: Robot power is on
        uint8_t bError : 1;       // Bit 3: Robot is in fault state
        uint8_t bBrakeTestOk : 1; // Bit 4: Brake test successful
        uint8_t bMasteringOk : 1; // Bit 5: Mastering successful
        uint8_t reserved1 : 2;

        // Mode Bits (1 byte total)
        uint8_t bInT1 : 1;  // Bit 0: Manual mode (Reduced speed)
        uint8_t bInT2 : 1;  // Bit 1: Manual mode (Full speed)
        uint8_t bInAut : 1; // Bit 2: Automatic mode
        uint8_t bInExt : 1; // Bit 3: External Automatic mode
        uint8_t reserved2 : 4;

        uint8_t nAreaFree_Robot; // Bitmask: Robot signals if Area [0..7] is free for PLC
        uint32_t nErrorCode;     // Active error code
    };
#pragma pack(pop)

    static_assert(sizeof(RobotControl) == 4, "RobotControl must be 4 bytes for ADS/TwinCAT compatibility");
    static_assert(sizeof(RobotStatus) == 9, "RobotStatus must be 9 bytes for ADS/TwinCAT compatibility");

    class RobotSimulator : public ISimulator
    {
      public:
        struct JobTrajectory
        {
            uint16_t jobId{ 0 };
            std::vector<Pose> poses;
        };

        struct Config
        {
            std::vector<JobTrajectory> jobTrajectories;
        };

        struct AdsSymbols
        {
            std::string controlSymbol{ "MAIN.stRobotControl" };
            std::string statusSymbol{ "MAIN.stRobotStatus" };
            std::string gripperSensorSymbol{ "MAIN.bGripperPartDetected" };
        };

        explicit RobotSimulator(std::shared_ptr<link::ILink> link);
        RobotSimulator(std::shared_ptr<link::ILink> link, Config config);
        RobotSimulator(std::shared_ptr<link::ILink> link, AdsSymbols adsSymbols, Config config = {});
        ~RobotSimulator() override;

        auto name() const -> std::string override { return "Robot"; }
        auto initialize() -> coro::Task<result::Result<void>> override;
        auto start() -> void override;
        auto stop() -> void override;
        auto update(double deltaTimeSeconds) -> void override;

        auto run() -> coro::Task<void>;

        // State Access
        auto control() const -> RobotControl;
        auto status() const -> RobotStatus;
        auto adsStatus() const -> std::string;
        auto jointAngles() const -> const double*;
        auto currentPose() const -> Pose;

        auto isGripperGripped() const -> bool;
        auto setGripper(bool gripped) -> void;
        auto isGripperSensorBlocked() const -> bool;
        auto setGripperSensorBlocked(bool blocked) -> void;

        auto setJointAngles(const double* anglesDegrees) -> void;
        auto setTargetPose(const Pose& pose) -> bool;
        auto triggerJob(uint16_t jobId) -> void;
        auto setAdsLink(std::shared_ptr<link::ILink> link) -> void;
        auto setInternalMode(bool internalMode) -> void;
        auto isInternalMode() const -> bool;
        auto setExternalCommandSimulationEnabled(bool enabled) -> void;
        auto externalCommandSimulationEnabled() const -> bool;

      private:
        enum class JobId : uint16_t
        {
            Home = 1,
            PickEntry = 2,
            PlaceLaser = 3,
            PickLaser = 4,
            PlaceExit = 7
        };

        static auto defaultJobTrajectories() -> std::vector<JobTrajectory>;
        auto configuredPosesForJob(uint16_t jobId) const -> const std::vector<Pose>*;
        auto planTrajectoryThroughPoses(const std::array<double, 6>& startJoints,
                                        const std::vector<Pose>& poses,
                                        std::vector<std::array<double, 6>>& outTrajectory) const -> bool;
        auto applyJobCompletionEffects(uint16_t jobId) -> void;

        auto planTrajectory(const std::array<double, 6>& startJoints,
                            const std::array<double, 6>& targetJoints) const
          -> std::vector<std::array<double, 6>>;

        std::shared_ptr<link::ILink> m_link;
        AdsSymbols m_adsSymbols;
        Kinematics m_kinematics;

        RobotControl m_control{};
        RobotStatus m_status{};
        double m_jointAngles[6]{};
        double m_targetJointAngles[6]{};
        uint16_t m_lastTargetJobId{ 0 };
        uint16_t m_lastSuccessfulJobId{ 0 };
        bool m_gripperGripped{ false };
        bool m_gripperSensorBlocked{ false };
        bool m_internalMode{ false };
        bool m_externalCommandSimulationEnabled{ true };
        bool m_manualTrajectoryActive{ false };
        std::unordered_map<uint16_t, std::vector<Pose>> m_jobTrajectories;

        std::vector<std::array<double, 6>> m_currentTrajectory;
        size_t m_trajectoryStep{ 0 };

        mutable std::mutex m_mutex;
        std::atomic<bool> m_running{ false };
    };
}