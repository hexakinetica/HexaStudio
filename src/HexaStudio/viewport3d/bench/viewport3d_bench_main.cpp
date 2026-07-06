// --- START OF FILE: HexaStudio/viewport3d/bench/viewport3d_bench_main.cpp ---
/**
 * @file viewport3d_bench_main.cpp
 * @brief Standalone bench hosting the module-owned ViewportPanel + FakeViewportController, NO
 *        RobotService, NO network, NO HexaStudio.
 *
 * Modes:
 *   (no args)            interactive: loads the robot, animates a pose, shows a demo trajectory.
 *   --selftest           headless smoke: loads the production URDF, asserts a valid 6-axis chain is
 *                        built and feeds one status + trajectory; exits 0 on success, 1 otherwise.
 *   --screenshot <png>   renders the cinematic scene to <png> (plus a top-view <png>_top.png) for
 *                        boss design-review without a manual run.
 *   --urdf <path>        override the default robot model URDF.
 *   --rx <deg>           override the root X tilt. Default 0 (production: the HexaArmMedium export
 *                        is Z-up, hexacore_config.json modelRootRxDeg = 0). The Y-up Mini export
 *                        needs --rx 90.
 */
#include <QApplication>
#include <QFileInfo>
#include <QImage>
#include <QRegularExpression>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

#include "ViewportPanel.h"
#include "FakeViewportController.h"

using hexa::FakeViewportController;
using hexa::ViewportPanel;

namespace {

#ifndef VIEWPORT3D_DEFAULT_URDF
#define VIEWPORT3D_DEFAULT_URDF ""
#endif

// Root X tilt, routed through THE root transform exactly as production does
// (hexacore_config.json: modelRootRxDeg), so the bench exercises the same single-transform path the
// controller uses. The active HexaArmMedium export is Z-up -> default 0; the Y-up Mini export needs
// --rx 90 to stand upright.
constexpr double kDefaultRootRxDeg = 0.0;

QString resolveUrdfPath(const QStringList& args) {
    const int idx = args.indexOf(QStringLiteral("--urdf"));
    if (idx >= 0 && idx + 1 < args.size()) {
        return args.at(idx + 1);
    }
    return QString::fromUtf8(VIEWPORT3D_DEFAULT_URDF);
}

double resolveRootRxDeg(const QStringList& args) {
    const int idx = args.indexOf(QStringLiteral("--rx"));
    if (idx >= 0 && idx + 1 < args.size()) {
        bool ok = false;
        const double value = args.at(idx + 1).toDouble(&ok);
        if (ok) {
            return value;
        }
        qWarning() << "viewport3d bench: invalid --rx value" << args.at(idx + 1)
                   << "- using default" << kDefaultRootRxDeg;
    }
    return kDefaultRootRxDeg;
}

QString screenshotPath(const QStringList& args) {
    const int idx = args.indexOf(QStringLiteral("--screenshot"));
    if (idx >= 0 && idx + 1 < args.size()) {
        return args.at(idx + 1);
    }
    return QString();
}

} // namespace

int main(int argc, char* argv[]) {
    // Required for a QQuickWidget hosting a QtQuick3D scene: the widget compositor and the Quick3D
    // render context must share GL resources, otherwise the RHI backing-store flush can crash.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);
    const QStringList args = app.arguments();

    const bool selftest = args.contains(QStringLiteral("--selftest"));
    const QString shotPath = screenshotPath(args);
    const QString urdfPath = resolveUrdfPath(args);
    const double rootRxDeg = resolveRootRxDeg(args);

    auto* host = new QWidget();
    host->setWindowTitle(QStringLiteral("viewport3d bench"));
    host->resize(1100, 760);
    auto* layout = new QVBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* panel = new ViewportPanel(host);
    layout->addWidget(panel);

    auto* fc = new FakeViewportController(&app);
    QObject::connect(fc, &FakeViewportController::statusChanged, panel, &ViewportPanel::updateState);

    const bool modelLoaded = panel->setRobotModelConfig(urdfPath, 0.0, 0.0, 0.0, rootRxDeg, 0.0, 0.0);

    // Demo PREVIEW with production semantics: the planned trajectory is DISPLAYED first (waypoint
    // spheres + thin line), then the robot RIDES it - not a trail growing behind the tip. The path is
    // precomputed by sampling the flange over one closed pose cycle of the fake controller.
    auto buildPreview = [panel, fc]() {
        HmiTrajectoryData preview;
        constexpr int kSamples = 120;
        constexpr int kPreviewWaypoints = 10;
        QVector<QVector3D> pathPoints;
        pathPoints.reserve(kSamples);
        for (int i = 0; i < kSamples; ++i) {
            const double phase = 360.0 * static_cast<double>(i) / static_cast<double>(kSamples);
            panel->updateState(fc->statusAt(phase));
            pathPoints.append(panel->flangeWorldPosition());
        }
        preview.path = pathPoints;
        const int step = std::max(1, kSamples / kPreviewWaypoints);
        for (int i = 0; i < pathPoints.size(); i += step) {
            preview.waypoints.append(pathPoints[i]);
        }
        preview.waypoints.append(pathPoints.first());   // closed cycle: the line returns to the start
        return preview;
    };

    if (selftest) {
        if (!modelLoaded) {
            qCritical() << "viewport3d selftest FAILED: no valid 6-axis chain from" << urdfPath;
            return 1;
        }
        const HmiTrajectoryData preview = buildPreview();
        if (preview.waypoints.size() < 3) {
            qCritical() << "viewport3d selftest FAILED: flange path preview is empty/degenerate.";
            return 1;
        }
        panel->updateTrajectoryPath(preview);
        panel->updateState(fc->statusAt(0.0));
        qInfo() << "viewport3d selftest OK: 6-axis chain, flange tip and trajectory preview built.";
        return 0;
    }

    host->show();

    const HmiTrajectoryData preview = buildPreview();
    panel->updateTrajectoryPath(preview);
    panel->updateState(fc->statusAt(0.0));   // the robot starts at the path start

    if (!shotPath.isEmpty()) {
        // Let the 3D scene load its meshes and render several frames, then grab.
        QTimer::singleShot(3500, &app, [panel, shotPath]() {
            const QImage iso = panel->grabViewport();
            if (iso.isNull() || !iso.save(shotPath)) {
                qCritical() << "viewport3d screenshot FAILED: could not grab/save" << shotPath;
                QCoreApplication::exit(1);
                return;
            }
            // A second top-view frame for the review.
            panel->setViewTop();
            QTimer::singleShot(500, qApp, [panel, shotPath]() {
                QString topPath = shotPath;
                topPath.replace(QRegularExpression("\\.png$", QRegularExpression::CaseInsensitiveOption),
                                QStringLiteral("_top.png"));
                panel->grabViewport().save(topPath);
                qInfo() << "viewport3d screenshot saved:" << shotPath << "and" << topPath;
                QCoreApplication::exit(0);
            });
        });
        return app.exec();
    }

    fc->start();
    return app.exec();
}
