// --- START OF FILE: HexaStudio/viewport3d/bench/FakeViewportController.h ---
/**
 * @file FakeViewportController.h
 * @brief Offline controller emulation for the viewport3d bench: feeds an animated robot status
 *        (commanded + physical joints for the ghost) and a demo trajectory, so the whole panel -
 *        chain, ghost, trajectory - renders with no RobotService and no network.
 *
 * Joint values are emitted in DEGREES (the panel drives QQuaternion::fromAxisAndAngle, which is
 * degree-based). The physical (ghost) joints lag the commanded ones on one axis so the Convention-B
 * ghost is visible.
 */
#ifndef HEXA_FAKE_VIEWPORT_CONTROLLER_H
#define HEXA_FAKE_VIEWPORT_CONTROLLER_H

#include <QObject>
#include <QTimer>

#include "BackendTypes.h"   // HmiMotionStatus, HmiTrajectoryData (hexa_contracts)

namespace hexa {

class FakeViewportController : public QObject {
    Q_OBJECT
public:
    explicit FakeViewportController(QObject* parent = nullptr);

    // Begin the animation timer (interactive bench): the robot rides the closed pose cycle.
    void start();

    // The pose at a given phase of the closed 360-deg cycle. Public so the bench can precompute the
    // flange path over one cycle (the displayed trajectory PREVIEW the robot then follows) and stage
    // fixed poses for selftest/screenshot.
    HmiMotionStatus statusAt(double phaseDeg) const;

signals:
    void statusChanged(const HmiMotionStatus& status);

private:
    void tick();

    QTimer m_timer;
    double m_phaseDeg = 0.0;
};

} // namespace hexa

#endif // HEXA_FAKE_VIEWPORT_CONTROLLER_H
