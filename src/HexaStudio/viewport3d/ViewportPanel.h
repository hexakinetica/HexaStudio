// --- START OF FILE: HexaStudio/viewport3d/ViewportPanel.h ---
/**
 * @file ViewportPanel.h
 * @brief Standalone, RobotService-free 3D viewport panel for the new-product viewport3d module.
 *
 * Module-owned re-implementation of the shipping PanelView3D (which is frozen). It keeps the exact
 * feedback/config contract PanelView3D exposes - so it drops into the same MainWindow/ShellWindow
 * wiring - but rebuilds the loader and the scene cleanly:
 *
 *   - Mesh + appearance pipeline (REQ 5.1): STL is the default; a higher-fidelity sibling
 *     (.glb/.gltf/.obj) auto-upgrades a link if present. STL links are shaded with SMOOTH normals and
 *     coloured from the URDF <material> (the shipping panel discarded the URDF colour and hardcoded
 *     one); advanced meshes keep their own PBR materials.
 *   - Coordinate-system correction (REQ 5.2): a mis-authored URDF frame is corrected by explicit,
 *     typed config (up-axis + fine RPY trim), separate from the physical mount transform and from the
 *     fixed render-only Z-up->Y-up conversion.
 *   - Terminal flange (audit fix): the visual chain honours the terminal fixed flange frame that the
 *     KDL FK chain already bakes in, so the rendered robot ends at the FK tip (no-op for a
 *     flange-less URDF).
 *   - Trajectory visualization (REQ 5.3): a dedicated TrajectoryVisual owns the trajectory subtree
 *     (instanced sample markers + one polyline + waypoints), replacing the shipping inline logic.
 *
 * NO RobotService / network coupling (the shipping panel already had none). Robot geometry comes from
 * the shared robot_model foundation lib (KinematicModel); the ghost keeps Convention B semantics
 * (solid = commanded pose, ghost = physical robot).
 */
#ifndef HEXA_VIEWPORT_PANEL_H
#define HEXA_VIEWPORT_PANEL_H

#include <QWidget>
#include <QVector>
#include <QQuaternion>
#include <QVector3D>
#include <QColor>
#include <QHash>
#include <QImage>
#include <QString>
#include <QElapsedTimer>

#include <memory>

#include "BackendTypes.h"   // HmiMotionStatus, HmiTrajectoryData (hexa_contracts)

class QQuickView;
class QQuick3DNode;
class QQuick3DModel;
class QQuick3DGeometry;
class QQuick3DPrincipledMaterial;
class QTimer;

namespace hexa {

class TrajectoryVisual;   // module-owned trajectory subtree, defined in ViewportPanel.cpp

/**
 * @brief Immutable-per-load configuration for the robot model shown in the viewport.
 *
 * ONE world->base root transform, mirroring the controller's RobotModelConfig.root_transform exactly
 * (the production config already routes the URDF up-axis fix through it: modelRootRxDeg = 90 in
 * hexacore_config.json). A single transform - instead of an extra "URDF frame correction" knob -
 * guarantees the visual robot, the KDL FK and the displayed trajectory can never disagree about the
 * base frame. The fixed render-only Z-up->Y-up conversion lives on the world node, NOT here.
 */
struct ViewportModelConfig {
    QString urdfPath;

    // World->base root transform (mount placement + any URDF frame fix), identical to the controller's.
    double mount_x_mm = 0.0;
    double mount_y_mm = 0.0;
    double mount_z_mm = 0.0;
    double mount_rx_deg = 0.0;
    double mount_ry_deg = 0.0;
    double mount_rz_deg = 0.0;
};

class ViewportPanel : public QWidget {
    Q_OBJECT
public:
    explicit ViewportPanel(QWidget* parent = nullptr);
    ~ViewportPanel() override;

    /**
     * @brief Load/replace the robot model and its world->base root transform.
     *
     * Signature-compatible with the shipping PanelView3D::setRobotModelConfig. The x/y/z/r* values are
     * THE root transform and must be fed from the same config the controller uses
     * (RobotModelConfig.root_transform / hexacore_config.json modelRoot*), so the visual robot, the
     * KDL FK and the displayed trajectory always share one base frame. A mis-authored URDF up-axis is
     * fixed here too (e.g. rx = 90 for a SolidWorks Y-up export), exactly as production does.
     *
     * @return true if a valid 6-axis visual chain was built.
     */
    bool setRobotModelConfig(const QString& urdfPath,
                             double xMm, double yMm, double zMm,
                             double rxDeg, double ryDeg, double rzDeg);

public slots:
    void updateState(const HmiMotionStatus& status);
    void updateTrajectoryPath(const HmiTrajectoryData& data);
    /// @brief Program-execution gate for the fade-executed trajectory + execution comet. A FRESH
    /// run restores full preview brightness; a RESUME from pause keeps the executed trace (the
    /// operator's record of where the program stopped). Fed from HmiProgramStatus.isRunning +
    /// isPaused by the shell.
    void setProgramExecutionState(bool isRunning, bool isPaused);
    void setGhostVisible(bool visible);
    void setTrajectoryVisible(bool visible);
    /// @brief Shows/hides the flange TCP marker (teal dot + XYZ triad) on the solid robot.
    void setTcpFrameVisible(bool visible);
    /// @brief Shows/hides the approach + departure transfer moves: the run-in from the current pose
    /// to the FIRST programmed waypoint and the run-out past the LAST one. When visible they draw in
    /// the secondary steel colour (distinct from the gold programmed path); when hidden those
    /// segments are not drawn at all.
    void setApproachVisible(bool visible);
    void setViewTop();
    void setViewFront();
    void setViewIso();
    void setViewSide();
    void setViewFitToScreen();

    // Render the current viewport frame to an image (used by the bench for design-review captures).
    // Uses QQuickWindow::grabWindow(), which is reliable offscreen unlike widget-texture grabs.
    QImage grabViewport();

    // Current flange (robot tip) position in the Z-up world frame - the same frame
    // updateTrajectoryPath() data is displayed in. Falls back to the last actuated joint when the
    // URDF defines no flange.
    QVector3D flangeWorldPosition() const;

protected:
    // Application-wide input watch: any deliberate operator input (click, wheel, key, touch)
    // suppresses the showroom turntable and re-arms its idle timer, so the camera never starts
    // orbiting while the operator is working in ANY panel (boss review 2026-07-07).
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // Forwards operator activity to the QML scene's notifyCameraInteraction (throttled to 250 ms).
    void notifyOperatorActivity();
    qint64 m_lastOperatorActivityMs = 0;

    // Number of actuated axes this viewport renders. The shared model is a fixed 6-axis manipulator.
    static constexpr int kRobotAxisCount = 6;

    struct RobotChain {
        QQuick3DNode* root_entity = nullptr;                       // whole-robot root (root transform)
        QVector<QQuick3DNode*> transforms;                         // the kRobotAxisCount actuated joints
        QVector<QQuick3DPrincipledMaterial*> materials;            // STL-link materials for restyling
        QQuick3DNode* flange_entity = nullptr;                     // terminal fixed flange frame (tool anchor)
        QQuick3DNode* tcp_marker = nullptr;                        // flange TCP marker (solid robot only)
    };

    // Result of attaching a mesh visual under a node (one path for base/link/flange).
    struct MeshVisual {
        QQuick3DPrincipledMaterial* material = nullptr;   // non-null only for STL links (asset meshes self-material)
        bool ok = false;
    };

    void setupUi();
    void setup3DScene();
    void rebuildRobotChains();
    void clearRobotChain(RobotChain& chain);
    void updateVisualState();

    bool loadRobotUrdfModel();
    RobotChain buildRobotChain(QQuick3DNode* rootParent, const QString& nameSuffix, bool isGhost);

    // Places the robot root: THE world->base root transform (identical to the controller's).
    void applyRootTransform();
    void applyRootTransformTo(QQuick3DNode* rootEntity, bool isGhost) const;

    // Display interpolation of the rendered joints (constants in the .cpp): the two latest status
    // packets per chain are replayed LINEARLY - constant velocity, exact arrival, one
    // packet-interval display latency. Display-only - the ghost-delta check, the executed-fade and
    // every command path keep using raw controller data.
    struct JointStream {
        QVector<double> prev;      // packet before last
        QVector<double> curr;      // latest packet
        qint64 prev_ms = 0;        // arrival stamps on m_smoothClock
        qint64 curr_ms = 0;
        QVector<double> shown;     // last pose applied to the scene (settle gate)
    };
    void applyJointsToChain(const QVector<double>& joints, RobotChain& robot);
    void pushJointPacket(const QVector<double>& joints, JointStream& stream, RobotChain& robot);
    void renderInterpolated(JointStream& stream, RobotChain& robot);
    void onSmoothTick();

    // Appearance/mesh pipeline (REQ 5.1). URDF parsing/mesh resolution live in the pure, unit-tested
    // loader (UrdfVisualModel.h); this method only attaches a resolved mesh to the scene. The teal TCP
    // highlight + XYZ triad is a stateless file-local helper (attachTcpMarker) in the .cpp.
    MeshVisual attachMeshVisual(QQuick3DNode* parentNode, const QString& meshPath, const QColor& urdfColor,
                                const QVector3D& visualOffset, const QQuaternion& visualRot,
                                bool isGhost, const QString& diagName);

    QQuickView* m_quickView = nullptr;      // hosted via createWindowContainer (native window)
    QQuick3DNode* m_sceneRoot = nullptr;
    // Fixed Z-up (URDF) -> Y-up (QtQuick3D) render conversion node. All world-frame content is parented
    // under this; floor/camera/lights stay Y-up.
    QQuick3DNode* m_worldRoot = nullptr;

    RobotChain m_mainRobot;
    RobotChain m_ghostRobot;

    // Parsed-STL cache keyed by absolute mesh path: the main and ghost chains render the SAME meshes,
    // so each STL is parsed and smooth-shaded once and the geometry object is shared by both models.
    // Owned by m_worldRoot (outlives both chains); cleared on every URDF (re)load.
    QHash<QString, QQuick3DGeometry*> m_stlGeometryCache;

    // Per-actuated-axis visual data (index 0..kRobotAxisCount-1).
    QVector<QQuaternion> m_jointRestRotations;
    QVector<QVector3D> m_jointAxes;
    QVector<QVector3D> m_jointOrigins;
    QVector<QVector3D> m_jointVisualOffsets;
    QVector<QQuaternion> m_jointVisualRotations;
    QVector<QString> m_jointMeshPaths;
    QVector<QString> m_jointLinkNames;
    QVector<QColor> m_jointLinkColors;   // URDF <material> colour per link (REQ 5.1)

    QString m_baseMeshPath;
    QColor m_baseColor;
    // Authored <visual><origin> of the base link. Was implicitly identity for the legacy exports;
    // a re-anchored URDF (HexaArmMedium: base frame moved to the mounting face) authors a non-zero
    // offset here and dropping it renders the base mesh detached from the arm.
    QVector3D m_baseVisualOffset;
    QQuaternion m_baseVisualRotation;

    // Terminal flange frame (fixed joint(s) past the 6 axes); empty for a flange-less URDF.
    bool m_hasFlangeFrame = false;
    QVector3D m_flangeOrigin;
    QQuaternion m_flangeRestRotation;
    QVector3D m_flangeVisualOffset;
    QQuaternion m_flangeVisualRotation;
    QString m_flangeMeshPath;
    QString m_flangeLinkName;
    QColor m_flangeColor;

    ViewportModelConfig m_modelConfig;
    bool m_isRebuildingChains = false;

    bool m_isGhostVisible = true;
    bool m_isTrajVisible = true;
    bool m_isApproachVisible = true;
    bool m_isTcpFrameVisible = true;
    bool m_hasGhostDelta = false;

    HmiTrajectoryData m_rawTrajectory;
    QVector3D m_lastRobotTcp;

    std::unique_ptr<TrajectoryVisual> m_trajectory;
    bool m_programRunning = false;   ///< Gate for the executed-trajectory fade (program runs only).
    bool m_wasPaused = false;        ///< Last HmiProgramStatus.isPaused (resume must keep the trace).

    QQuick3DNode* m_tcpMarkerEntity = nullptr;

    // Joint display-interpolation state + the camera-automation motion gate.
    QTimer* m_smoothTimer = nullptr;
    QElapsedTimer m_smoothClock;
    JointStream m_mainJointStream;
    JointStream m_ghostJointStream;
    bool m_lastMotionActiveForCamera = false;   ///< Last motionActive pushed to the QML scene.

    // TEMPORARY DIAGNOSTIC (progressive-slowdown investigation, 0.6.18 - remove when closed).
    QVector<double> m_diagPrevJoints;
    qint64 m_diagWindowStartMs = 0;
    int m_diagPackets = 0;
    int m_diagPoseChanges = 0;
    double m_diagStepSumDeg = 0.0;
};

} // namespace hexa

#endif // HEXA_VIEWPORT_PANEL_H
