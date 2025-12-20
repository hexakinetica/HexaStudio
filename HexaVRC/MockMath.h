/**
 * @file MockMath.h
 * @brief Linear algebra and Kinematics helper library.
 * @author HexaKinetica Team
 * @version 1.0
 *
 * This header-only library provides the necessary mathematical primitives
 * (4x4 Matrices, Trigonometry helpers) to calculate Forward Kinematics (FK)
 * for the robot simulation. It avoids external dependencies like Eigen or GLM
 * to keep the Mock Controller lightweight.
 */

#ifndef MOCKMATH_H
#define MOCKMATH_H

#include <array>
#include <cmath>
#include <vector>
#include "../Shared/RdtProtocol.h"

namespace Math {

constexpr double PI = 3.14159265358979323846;
constexpr double DEG2RAD = PI / 180.0; ///< Multiplier to convert Degrees to Radians.
constexpr double RAD2DEG = 180.0 / PI; ///< Multiplier to convert Radians to Degrees.

/**
 * @brief 4x4 Homogeneous Transformation Matrix.
 * @details Row-major storage. Used for coordinate transformations.
 */
struct Mat4 {
    /// @brief Internal array: [row0, row1, row2, row3].
    std::array<double, 16> m = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };

    /// @brief Returns an Identity matrix.
    static Mat4 Identity() { return Mat4(); }

    /// @brief Matrix multiplication.
    Mat4 operator*(const Mat4& r) const {
        Mat4 res;
        for(int row=0; row<4; ++row) {
            for(int col=0; col<4; ++col) {
                double sum = 0;
                for(int k=0; k<4; ++k) sum += m[row*4 + k] * r.m[k*4 + col];
                res.m[row*4 + col] = sum;
            }
        }
        return res;
    }

    /// @brief Creates a translation matrix.
    static Mat4 Translate(double x, double y, double z) {
        Mat4 res; res.m[3]=x; res.m[7]=y; res.m[11]=z; return res;
    }

    /// @brief Creates a rotation matrix around Z axis (Roll).
    static Mat4 RotZ(double rad) {
        Mat4 res; double c=cos(rad), s=sin(rad);
        res.m[0]=c; res.m[1]=-s; res.m[4]=s; res.m[5]=c; return res;
    }
    /// @brief Creates a rotation matrix around Y axis (Pitch).
    static Mat4 RotY(double rad) {
        Mat4 res; double c=cos(rad), s=sin(rad);
        res.m[0]=c; res.m[2]=s; res.m[8]=-s; res.m[10]=c; return res;
    }
    /// @brief Creates a rotation matrix around X axis (Yaw).
    static Mat4 RotX(double rad) {
        Mat4 res; double c=cos(rad), s=sin(rad);
        res.m[5]=c; res.m[6]=-s; res.m[9]=s; res.m[10]=c; return res;
    }

    /**
     * @brief Creates a rotation matrix from Euler Angles (ZYX convention).
     * @details Corresponds to URDF rotation logic (roll, pitch, yaw).
     */
    static Mat4 FromURDF(double roll_deg, double pitch_deg, double yaw_deg) {
        return RotZ(yaw_deg * DEG2RAD) * RotY(pitch_deg * DEG2RAD) * RotX(roll_deg * DEG2RAD);
    }

    /**
     * @brief Creates a full transformation matrix from a 6D pose.
     * @param p Array {x, y, z, rx, ry, rz}.
     */
    static Mat4 FromPose(const std::array<double, 6>& p) {
        return Translate(p[0], p[1], p[2]) * FromURDF(p[3], p[4], p[5]);
    }

    /**
     * @brief Creates the specific transform for a robotic joint.
     * @details Combines the static offset of the joint (r, p, y) with the dynamic rotation (jointVal).
     */
    static Mat4 JointTransform(double r, double p, double y, double jointVal_deg) {
        return FromURDF(r, p, y) * RotZ(jointVal_deg * DEG2RAD);
    }

    /**
     * @brief Calculates the inverse of the matrix.
     * @note Assumes the matrix is a rigid body transform (Rotation + Translation).
     * Simple transpose-based inversion for rotation part.
     */
    Mat4 Inverse() const {
        Mat4 res;
        // Transpose rotation part
        for(int i=0; i<3; ++i) for(int j=0; j<3; ++j) res.m[i*4+j] = m[j*4+i];

        // Calculate new translation: -R^T * t
        double tx=m[3], ty=m[7], tz=m[11];
        res.m[3] = -(res.m[0]*tx + res.m[1]*ty + res.m[2]*tz);
        res.m[7] = -(res.m[4]*tx + res.m[5]*ty + res.m[6]*tz);
        res.m[11]= -(res.m[8]*tx + res.m[9]*ty + res.m[10]*tz);
        return res;
    }

    /**
     * @brief Extracts Euler Angles (ZYX) and Translation from the matrix.
     * @return Array {x, y, z, rx, ry, rz}.
     */
    std::array<double, 6> ToPose() const {
        double x = m[3]; double y = m[7]; double z = m[11];
        double sy = sqrt(m[0]*m[0] + m[4]*m[4]);
        bool singular = sy < 1e-6;
        double rx, ry, rz;
        if (!singular) {
            rx = atan2(m[9], m[10]);
            ry = atan2(-m[8], sy);
            rz = atan2(m[4], m[0]);
        } else {
            rx = atan2(-m[6], m[5]);
            ry = atan2(-m[8], sy);
            rz = 0;
        }
        return {x, y, z, rx*RAD2DEG, ry*RAD2DEG, rz*RAD2DEG};
    }

    /// @brief Extract only the translation part (X, Y, Z).
    Rdt::TrajectoryPoint ToXYZ() const { return {m[3], m[7], m[11]}; }
};

/**
 * @brief Calculates Forward Kinematics for the 'lbr_iisy11_r1300' robot.
 * @details Hardcoded DH-like parameters extracted from the robot's URDF.
 * @param joints_deg Array of 6 joint angles in degrees.
 * @return The transformation matrix of the Flange (Tool0) relative to the Base.
 */
inline Mat4 CalculateFK(const std::array<double, 6>& joints_deg) {
    Mat4 T = Mat4::Identity();

    // 1. Joint 1 (URDF: 0 0 0.1845 | 180 0 0)
    T = T * Mat4::Translate(0, 0, 184.5) * Mat4::JointTransform(180, 0, 0, joints_deg[0]);

    // 2. Joint 2 (URDF: 0 0.1011 -0.1155 | 90 0 0)
    T = T * Mat4::Translate(0, 101.1, -115.5) * Mat4::JointTransform(90, 0, 0, joints_deg[1]);

    // 3. Joint 3 (URDF: 0.59 0 0.0237 | 0 0 0)
    T = T * Mat4::Translate(590.0, 0, 23.7) * Mat4::JointTransform(0, 0, 0, joints_deg[2]);

    // 4. Joint 4 (URDF: 0.1139 0 0.0774 | 90 0 -90)
    T = T * Mat4::Translate(113.9, 0, 77.4) * Mat4::JointTransform(90, 0, -90, joints_deg[3]);

    // 5. Joint 5 (URDF: 0 0.0507 -0.4181 | 0 90 90)
    T = T * Mat4::Translate(0, 50.7, -418.1) * Mat4::JointTransform(0, 90, 90, joints_deg[4]);

    // 6. Joint 6 (URDF: 0.0837 0 -0.0507 | 90 0 -90)
    T = T * Mat4::Translate(83.7, 0, -50.7) * Mat4::JointTransform(90, 0, -90, joints_deg[5]);

    // Link 6 -> Tool0 (Flange)
    // URDF: xyz="0.0 0.0 -0.0943" rpy="0.0 ${pi} 0.0" -> RotY(180)
    T = T * Mat4::Translate(0, 0, -94.3) * Mat4::FromURDF(0, 180, 0);

    return T;
}

}

#endif // MOCKMATH_H
