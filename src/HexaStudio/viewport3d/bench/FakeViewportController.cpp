// --- START OF FILE: HexaStudio/viewport3d/bench/FakeViewportController.cpp ---
#include "FakeViewportController.h"

#include <QtMath>
#include <QVector3D>

namespace hexa {

namespace {
constexpr int kTickIntervalMs = 33;    // ~30 fps animation
constexpr double kPhaseStepDeg = 0.8;  // slow, smooth cycle
} // namespace

FakeViewportController::FakeViewportController(QObject* parent) : QObject(parent) {
    m_timer.setInterval(kTickIntervalMs);
    QObject::connect(&m_timer, &QTimer::timeout, this, &FakeViewportController::tick);
}

void FakeViewportController::start() {
    m_timer.start();   // each tick advances the closed pose cycle; the robot rides the displayed path
}

void FakeViewportController::tick() {
    m_phaseDeg += kPhaseStepDeg;
    if (m_phaseDeg >= 360.0) {
        m_phaseDeg -= 360.0;   // closed cycle: pose(360) == pose(0), no jump at wrap-around
    }
    emit statusChanged(statusAt(m_phaseDeg));
}

HmiMotionStatus FakeViewportController::statusAt(double phaseDeg) const {
    const double p = qDegreesToRadians(phaseDeg);

    // A photogenic 6-axis pose cycle. All axes use the SAME base frequency with phase offsets, so the
    // cycle is CLOSED over one 360-deg phase turn: pose(0) == pose(360). The flange therefore traces a
    // closed loop, and the robot can ride the displayed trajectory forever with no jump at wrap-around.
    // Values are in DEGREES (panel is degree-based).
    constexpr double kDeg60 = 60.0 * M_PI / 180.0;
    constexpr double kDeg120 = 120.0 * M_PI / 180.0;
    constexpr double kDeg200 = 200.0 * M_PI / 180.0;
    constexpr double kDeg90 = 90.0 * M_PI / 180.0;
    QVector<double> commanded = {
        70.0 * std::sin(p),
        -20.0 + 30.0 * std::sin(p + kDeg60),
        45.0 * std::sin(p + kDeg120),
        25.0 * std::sin(2.0 * p),
        35.0 + 25.0 * std::sin(p + kDeg200),
        60.0 * std::sin(p + kDeg90)
    };

    // The physical robot (ghost) trails the command on axis 2 by a few degrees, so the Convention-B
    // ghost is meaningfully different and shows.
    QVector<double> physical = commanded;
    physical[1] += 6.0;
    physical[2] -= 4.0;

    HmiMotionStatus status;
    status.plannedJoints = commanded;
    status.realHardwareJoints = physical;
    status.actualJoints = physical;
    // No FK here; leave the Cartesian TCP unset so the marker stays hidden (it would otherwise float).
    return status;
}

} // namespace hexa
