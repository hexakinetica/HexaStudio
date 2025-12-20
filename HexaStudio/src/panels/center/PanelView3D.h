/**
 * @file PanelView3D.h
 * @brief Header for the 3D Visualization Panel using Qt3D.
 * @author HexaKinetica Team
 * @version 1.0
 *
 * This file defines the widget responsible for rendering the digital twin of the robot.
 * It uses the Qt3D module (Qt3DCore, Qt3DRender, Qt3DExtras) to manage the Scene Graph.
 */

#ifndef PANELVIEW3D_H
#define PANELVIEW3D_H

#include <QWidget>
#include <QVector>
#include <QQuaternion>
#include <QVector3D>
#include "../../backend/BackendTypes.h"

// Forward Declarations to speed up compilation
namespace Qt3DCore {
class QEntity;
class QTransform;
class QGeometry;
}
namespace Qt3DRender {
class QCamera;
class QGeometryRenderer;
}
namespace Qt3DExtras {
class Qt3DWindow;
class QOrbitCameraController;
class QPhongAlphaMaterial;
class QPhongMaterial;
}

namespace RDT {

/**
 * @brief The 3D Viewport widget.
 *
 * @details
 * **Scene Structure:**
 * - **Root Entity:** Top-level container.
 * - **Floor:** Reference plane.
 * - **Main Robot:** Represents the "Target/Planned" state (Solid opaque model).
 * - **Ghost Robot:** Represents the "Actual" state (Semi-transparent model). Only visible when states differ.
 * - **Trajectory:** A dynamically updated LineStrip and Points visualization.
 *
 * **Rendering:**
 * Uses a Forward Renderer with a custom shader approach for the trajectory lines
 * to support point sizing and coloring.
 */
class PanelView3D : public QWidget
{
    Q_OBJECT
public:
    explicit PanelView3D(QWidget *parent = nullptr);
    ~PanelView3D();

public slots:
    /**
     * @brief Updates the joint angles of the robot models.
     * @details Applies rotations to the QTransform components of both the Main and Ghost robots.
     * Also handles the visibility logic for the Ghost robot (hidden if simulation mode matches).
     * @param status The kinematic state (planned and actual joints).
     */
    void updateState(const HmiMotionStatus &status);

    /**
     * @brief Updates the geometry of the trajectory line.
     * @details Updates the raw Vertex Buffer Object (VBO) data for the path and waypoints.
     * @param data The new path points.
     */
    void updateTrajectoryPath(const HmiTrajectoryData &data);

    /**
     * @brief Toggles the visibility of the "Actual" (Ghost) robot model.
     */
    void setGhostVisible(bool visible);

    /**
     * @brief Toggles the visibility of the trajectory path.
     */
    void setTrajectoryVisible(bool visible);

    // --- Camera Controls ---
    void setViewTop();
    void setViewFront();
    void setViewIso();

private slots:
    /**
     * @brief Clamps camera position to prevent clipping or going under the floor.
     */
    void enforceCameraLimits();

private:
    void setupUi();
    void setup3DScene();

    /**
     * @brief Refreshes material properties (colors, transparency) based on current mode.
     */
    void updateVisualState();

    /**
     * @brief Struct representing a kinematic chain in the Scene Graph.
     */
    struct RobotChain {
        Qt3DCore::QEntity* rootEntity;
        /// @brief List of transforms for each joint (J1..J6).
        QVector<Qt3DCore::QTransform*> transforms;
        /// @brief List of materials to allow dynamic color changing.
        QVector<Qt3DExtras::QPhongAlphaMaterial*> materials;
    };

    /**
     * @brief Constructs a robot entity hierarchy.
     * @details Manually assembles the Scene Graph (Entity -> Transform -> Mesh -> Material)
     * effectively hardcoding the URDF structure.
     * @param rootParent Parent entity.
     * @param nameSuffix Unique suffix for entity names.
     * @param showTcp Whether to attach a TCP marker (sphere) to the last link.
     */
    RobotChain buildRobotChain(Qt3DCore::QEntity *rootParent, const QString &nameSuffix, bool showTcp = false);

    struct LinkComponents {
        Qt3DCore::QEntity* jointEntity;
        Qt3DCore::QTransform* transform;
        Qt3DExtras::QPhongAlphaMaterial* material;
    };

    /**
     * @brief Helper to create a single robot link.
     */
    LinkComponents createLink(Qt3DCore::QEntity *parentEntity, const QString &meshPath, const QVector3D &originPos, const QQuaternion &originRot);

    // --- Qt3D Core Objects ---
    QWidget *m_container;
    Qt3DExtras::Qt3DWindow *m_view;
    Qt3DCore::QEntity *m_rootEntity;
    Qt3DRender::QCamera *m_camera;
    Qt3DExtras::QOrbitCameraController *m_camController;

    // --- Robots ---
    RobotChain m_mainRobot;
    RobotChain m_ghostRobot;

    /// @brief Initial rotations from the "Zero" pose (offsets from CAD origin).
    QVector<QQuaternion> m_jointRestRotations;

    // --- State ---
    bool m_isRealMode = false;
    bool m_isGhostVisible = true;
    bool m_isTrajVisible = true;

    // --- Trajectory Resources (Raw OpenGL Buffers) ---
    Qt3DCore::QEntity *m_trajEntity = nullptr;
    Qt3DRender::QGeometryRenderer *m_trajRenderer = nullptr;
    Qt3DCore::QGeometry *m_trajGeometry = nullptr;

    Qt3DCore::QEntity *m_waypointEntity = nullptr;
    Qt3DRender::QGeometryRenderer *m_waypointRenderer = nullptr;
    Qt3DCore::QGeometry *m_waypointGeometry = nullptr;
};

} // namespace RDT

#endif // PANELVIEW3D_H
