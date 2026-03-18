#pragma once

#include <array>
#include <memory>
#include <vector>

namespace backend
{
    struct RobotPose
    {
        double x{ 0.0 };
        double y{ 0.0 };
        double z{ 0.0 };
        double roll{ 0.0 };
        double pitch{ 0.0 };
        double yaw{ 0.0 };
    };

    class RobotKinematics
    {
      public:
        RobotKinematics();
        ~RobotKinematics();

        auto forward(const std::array<double, 6>& jointAnglesRadians) const -> RobotPose;
        auto inverse(const RobotPose& target, const std::array<double, 6>& seedRadians) const
          -> std::vector<double>;

      private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}