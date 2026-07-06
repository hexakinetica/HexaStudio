// --- START OF FILE: HexaStudio/viewport3d/UrdfVisualModel.h ---
/**
 * @file UrdfVisualModel.h
 * @brief Pure, scene-graph-free description of the robot's visual chain loaded from a URDF.
 *
 * Produced by loadUrdfVisualModel() and consumed by ViewportPanel to build the QtQuick3D scene. Kept
 * free of Quick3D deliberately: the exact logic that has produced real defects in this project -
 * terminal-flange composition, authored-colour propagation, mesh path resolution - is unit-testable
 * here without a GPU (see viewport3d_tests).
 */
#ifndef HEXA_URDF_VISUAL_MODEL_H
#define HEXA_URDF_VISUAL_MODEL_H

#include <QString>
#include <QVector>
#include <QVector3D>
#include <QQuaternion>
#include <QColor>

namespace hexa {

// One link's visual: resolved mesh path (empty when the link has no visual mesh), the visual <origin>
// and the authored URDF <material> colour (fallback grey when the URDF declares none).
struct UrdfLinkVisual {
    QString meshPath;
    QVector3D visualOffsetMm;
    QQuaternion visualRotation;
    QColor color;
};

// One actuated axis of the visual chain: the child link's visual plus the joint placement.
struct UrdfAxisVisual {
    QString linkName;
    UrdfLinkVisual visual;
    QVector3D originMm;          // joint origin in the parent-joint frame
    QQuaternion restRotation;    // joint origin rotation (rest pose)
    QVector3D axis;              // rotation axis in the joint frame
};

struct UrdfVisualModel {
    static constexpr int kAxisCount = 6;

    bool valid = false;
    QString error;               // what failed and where, when !valid

    UrdfLinkVisual base;
    QVector<UrdfAxisVisual> axes;   // exactly kAxisCount entries when valid

    // Terminal flange frame: the fixed joint(s) past the actuated axes, composed into one transform
    // expressed in the last-axis frame (the same tip offset the KDL FK chain bakes into its tip
    // segment). Absent (hasFlange == false) for a flange-less URDF.
    bool hasFlange = false;
    QString flangeLinkName;
    QVector3D flangeOriginMm;
    QQuaternion flangeRestRotation;
    UrdfLinkVisual flangeVisual;    // meshPath empty for a bare flange frame
};

/**
 * @brief Loads the robot's visual chain from a URDF file. Pure data, no scene side effects.
 *
 * Uses the shared robot_model (KinematicModel) for parsing, so the visual chain and the authored
 * colours come from the same source of truth the controller uses.
 */
UrdfVisualModel loadUrdfVisualModel(const QString& urdfPath, const QString& baseLinkName);

/**
 * @brief Mesh auto-upgrade: prefer a higher-fidelity sibling (.glb/.gltf/.obj, in that order) with the
 * same basename next to the STL; return the STL path unchanged when none exists.
 */
QString resolvePreferredMesh(const QString& stlPath);

} // namespace hexa

#endif // HEXA_URDF_VISUAL_MODEL_H
