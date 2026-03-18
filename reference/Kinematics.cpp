#include "Kinematics.hpp"
#include "Logger/Logger.hpp"
#include <chain.hpp>
#include <chainfksolverpos_recursive.hpp>
#include <chainiksolverpos_lma.hpp>
#include <frames_io.hpp>

namespace core::sim
{
    struct Kinematics::Impl
    {
        KDL::Chain chain;
        std::unique_ptr<KDL::ChainFkSolverPos_recursive> fkSolver;
        std::unique_ptr<KDL::ChainIkSolverPos_LMA> ikSolver;

        Impl()
        {
            using namespace KDL;
            
            // Replicating the RobotModel.qml structure (Units in mm)
            // 1. Base to Joint 1
            chain.addSegment(Segment(Joint(Joint::None), Frame(Vector(0, 0, 450))));
            
            // 2. Joint 1 (RotZ) to Joint 2
            chain.addSegment(Segment(Joint(Joint::RotZ), Frame(Vector(150, 0, 0))));
            
            // 3. Joint 2 (RotY) to Joint 3
            chain.addSegment(Segment(Joint(Joint::RotY), Frame(Vector(610, 0, 0))));
            
            // 4. Joint 3 (RotY) to Joint 4
            chain.addSegment(Segment(Joint(Joint::RotY), Frame(Vector(0, 0, 20))));
            
            // 5. Joint 4 (RotX) to Joint 5
            chain.addSegment(Segment(Joint(Joint::RotX), Frame(Vector(660, 0, 0))));
            
            // 6. Joint 5 (RotY) to Joint 6
            chain.addSegment(Segment(Joint(Joint::RotY), Frame(Vector(80, 0, 0))));
            
            // 7. Joint 6 (RotX) to Tool0
            chain.addSegment(Segment(Joint(Joint::RotX), Frame(Vector(15, 0, 0))));

            fkSolver = std::make_unique<ChainFkSolverPos_recursive>(chain);
            
            // LMA solver is more robust for 6DOF than simple NR.
            // Since we use mm, we should provide weights to balance translation and rotation.
            Eigen::Matrix<double, 6, 1> weights;
            weights << 1, 1, 1, 0.01, 0.01, 0.01; // Scale down rotation error impact compared to mm translation
            ikSolver = std::make_unique<ChainIkSolverPos_LMA>(chain, weights, 1e-5, 1000, 1e-12);
        }
    };

    Kinematics::Kinematics() : m_impl(std::make_unique<Impl>()) {}
    Kinematics::~Kinematics() = default;

    auto Kinematics::forward(const std::array<double, 6>& jointAngles) const -> Pose
    {
        KDL::JntArray jntPos(6);
        // Match signs with RobotModel.qml: axis1, axis4, axis6 are inverted
        jntPos(0) = -jointAngles[0];
        jntPos(1) =  jointAngles[1];
        jntPos(2) =  jointAngles[2];
        jntPos(3) = -jointAngles[3];
        jntPos(4) =  jointAngles[4];
        jntPos(5) = -jointAngles[5];

        KDL::Frame cartPos;
        m_impl->fkSolver->JntToCart(jntPos, cartPos);

        Pose pose;
        pose.x = cartPos.p.x();
        pose.y = cartPos.p.y();
        pose.z = cartPos.p.z();
        cartPos.M.GetRPY(pose.roll, pose.pitch, pose.yaw);
        return pose;
    }

    auto Kinematics::inverse(const Pose& target, const std::array<double, 6>& seed) const -> std::vector<double>
    {
        KDL::JntArray jntSeed(6);
        jntSeed(0) = -seed[0];
        jntSeed(1) =  seed[1];
        jntSeed(2) =  seed[2];
        jntSeed(3) = -seed[3];
        jntSeed(4) =  seed[4];
        jntSeed(5) = -seed[5];

        KDL::Frame cartGoal(KDL::Rotation::RPY(target.roll, target.pitch, target.yaw), 
                            KDL::Vector(target.x, target.y, target.z));

        KDL::JntArray jntResult(6);
        int ret = m_impl->ikSolver->CartToJnt(jntSeed, cartGoal, jntResult);

        if (ret >= 0) {
            std::vector<double> result(6);
            result[0] = -jntResult(0);
            result[1] =  jntResult(1);
            result[2] =  jntResult(2);
            result[3] = -jntResult(3);
            result[4] =  jntResult(4);
            result[5] = -jntResult(5);
            return result;
        }

        logger::error("Kinematics: IK failed with error code {} for Target [X: {:.3f}, Y: {:.3f}, Z: {:.3f}]", 
                      ret, target.x, target.y, target.z);
        return {};
    }
}
