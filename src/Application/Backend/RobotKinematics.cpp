#include "RobotKinematics.h"

#include <chain.hpp>
#include <chainfksolverpos_recursive.hpp>
#include <chainiksolverpos_lma.hpp>

#include <Eigen/Core>

namespace backend
{
    struct RobotKinematics::Impl
    {
        KDL::Chain chain;
        std::unique_ptr<KDL::ChainFkSolverPos_recursive> fkSolver;
        std::unique_ptr<KDL::ChainIkSolverPos_LMA> ikSolver;

        Impl()
        {
            using namespace KDL;

            chain.addSegment(Segment(Joint(Joint::None), Frame(Vector(0, 0, 450))));
            chain.addSegment(Segment(Joint(Joint::RotZ), Frame(Vector(150, 0, 0))));
            chain.addSegment(Segment(Joint(Joint::RotY), Frame(Vector(610, 0, 0))));
            chain.addSegment(Segment(Joint(Joint::RotY), Frame(Vector(0, 0, 20))));
            chain.addSegment(Segment(Joint(Joint::RotX), Frame(Vector(660, 0, 0))));
            chain.addSegment(Segment(Joint(Joint::RotY), Frame(Vector(80, 0, 0))));
            chain.addSegment(Segment(Joint(Joint::RotX), Frame(Vector(15, 0, 0))));

            fkSolver = std::make_unique<ChainFkSolverPos_recursive>(chain);

            Eigen::Matrix<double, 6, 1> weights;
            weights << 1.0, 1.0, 1.0, 0.01, 0.01, 0.01;
            ikSolver = std::make_unique<ChainIkSolverPos_LMA>(chain, weights, 1e-5, 1000, 1e-12);
        }
    };

    RobotKinematics::RobotKinematics()
      : m_impl(std::make_unique<Impl>())
    {
    }

    RobotKinematics::~RobotKinematics() = default;

    auto RobotKinematics::forward(const std::array<double, 6>& jointAnglesRadians) const -> RobotPose
    {
        KDL::JntArray joints(6);
        joints(0) = -jointAnglesRadians[0];
        joints(1) = jointAnglesRadians[1];
        joints(2) = jointAnglesRadians[2];
        joints(3) = -jointAnglesRadians[3];
        joints(4) = jointAnglesRadians[4];
        joints(5) = -jointAnglesRadians[5];

        KDL::Frame frame;
        m_impl->fkSolver->JntToCart(joints, frame);

        RobotPose pose;
        pose.x = frame.p.x();
        pose.y = frame.p.y();
        pose.z = frame.p.z();
        frame.M.GetRPY(pose.roll, pose.pitch, pose.yaw);
        return pose;
    }

    auto RobotKinematics::inverse(const RobotPose& target, const std::array<double, 6>& seedRadians) const
      -> std::vector<double>
    {
        KDL::JntArray seed(6);
        seed(0) = -seedRadians[0];
        seed(1) = seedRadians[1];
        seed(2) = seedRadians[2];
        seed(3) = -seedRadians[3];
        seed(4) = seedRadians[4];
        seed(5) = -seedRadians[5];

        const KDL::Frame goal(
          KDL::Rotation::RPY(target.roll, target.pitch, target.yaw), KDL::Vector(target.x, target.y, target.z));

        KDL::JntArray result(6);
        if (m_impl->ikSolver->CartToJnt(seed, goal, result) < 0) {
            return {};
        }

        std::vector<double> joints(6);
        joints[0] = -result(0);
        joints[1] = result(1);
        joints[2] = result(2);
        joints[3] = -result(3);
        joints[4] = result(4);
        joints[5] = -result(5);
        return joints;
    }
}