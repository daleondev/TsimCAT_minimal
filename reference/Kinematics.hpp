#pragma once

#include <memory>
#include <vector>
#include <array>
#include <string>

namespace core::sim
{
    struct Pose
    {
        double x, y, z;
        double roll, pitch, yaw; // Euler angles in radians
    };

    class Kinematics
    {
      public:
        Kinematics();
        ~Kinematics();

        /**
         * Calculate Forward Kinematics.
         * @param jointAngles Radians
         * @return Pose
         */
        auto forward(const std::array<double, 6>& jointAngles) const -> Pose;

        /**
         * Calculate Inverse Kinematics.
         * @param target Task space pose
         * @param seed Joint angles to start search from (Radians)
         * @return Joint angles in Radians, or empty if no solution found
         */
        auto inverse(const Pose& target, const std::array<double, 6>& seed) const -> std::vector<double>;

      private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
