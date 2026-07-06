// --- START OF FILE: HexaStudio/viewport3d/UrdfVisualModel.cpp ---
#include "UrdfVisualModel.h"

#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QtMath>

#include "KinematicModel.h"   // shared robot_model: URDF -> ordered visual chain + authored colours

namespace hexa {

namespace {

// URDF translations are authored in meters; the viewport scene works in millimeters.
constexpr float kMetersToMm = 1000.0f;

// Neutral grey for links whose URDF declares no <material> colour.
const QColor kFallbackLinkColor("#9AA3AB");

QQuaternion urdfRpyToQuaternion(const double rpy[3]) {
    const float roll = static_cast<float>(qRadiansToDegrees(rpy[0]));
    const float pitch = static_cast<float>(qRadiansToDegrees(rpy[1]));
    const float yaw = static_cast<float>(qRadiansToDegrees(rpy[2]));
    const QQuaternion qRoll = QQuaternion::fromAxisAndAngle(1.0f, 0.0f, 0.0f, roll);
    const QQuaternion qPitch = QQuaternion::fromAxisAndAngle(0.0f, 1.0f, 0.0f, pitch);
    const QQuaternion qYaw = QQuaternion::fromAxisAndAngle(0.0f, 0.0f, 1.0f, yaw);
    return qYaw * qPitch * qRoll;
}

QVector3D urdfXyzToMm(const double xyz[3]) {
    return QVector3D(static_cast<float>(xyz[0]), static_cast<float>(xyz[1]), static_cast<float>(xyz[2]))
           * kMetersToMm;
}

// Resolve a URDF mesh URI (package://... or relative) against the URDF's package root.
QString resolveMeshPath(const QString& meshUri, const QString& urdfDir) {
    if (QFileInfo(meshUri).isAbsolute() && QFileInfo::exists(meshUri)) {
        return meshUri;
    }
    const QString packageRoot = QDir(urdfDir).absoluteFilePath("..");
    if (meshUri.startsWith("package://")) {
        QString rel = meshUri;
        rel.remove(0, QString("package://").size());
        const int slashPos = rel.indexOf('/');
        if (slashPos >= 0) {
            rel = rel.mid(slashPos + 1);
        }
        return QDir::cleanPath(QDir(packageRoot).absoluteFilePath(rel));
    }
    return QDir::cleanPath(QDir(packageRoot).absoluteFilePath(meshUri));
}

} // namespace

QString resolvePreferredMesh(const QString& stlPath) {
    const QFileInfo fi(stlPath);
    const QString base = fi.absolutePath() + "/" + fi.completeBaseName();
    for (const QString& ext : {QStringLiteral(".glb"), QStringLiteral(".gltf"), QStringLiteral(".obj")}) {
        const QString candidate = base + ext;
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return stlPath;
}

UrdfVisualModel loadUrdfVisualModel(const QString& urdfPath, const QString& baseLinkName) {
    UrdfVisualModel result;

    if (urdfPath.isEmpty() || !QFileInfo::exists(urdfPath)) {
        result.error = QStringLiteral("URDF file not found: '%1'").arg(urdfPath);
        return result;
    }

    RDT::RobotModelConfig kmConfig;
    kmConfig.urdf_path = urdfPath.toStdString();
    kmConfig.base_link = baseLinkName.toStdString();
    kmConfig.tip_link.clear();
    // The visual chain is pure URDF; the world->base root transform is applied on the visual root by
    // the panel (mirroring the controller), so the model is built with an identity root here.
    kmConfig.root_transform = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};

    const auto model = RDT::KinematicModel::createFromURDFFile(kmConfig);
    if (!model) {
        result.error = QStringLiteral("KinematicModel failed to load URDF '%1'").arg(urdfPath);
        return result;
    }

    const auto& description = model->getRobotDescription();
    const QString urdfDir = QFileInfo(urdfPath).absolutePath();

    struct VisualData {
        UrdfLinkVisual visual;
        bool meshValid = false;
    };
    QMap<QString, VisualData> linkToVisual;

    for (const auto& seg : description.segments) {
        const QString linkName = QString::fromStdString(seg.link_name);
        if (linkName.isEmpty()) {
            continue;
        }
        VisualData data;
        data.visual.meshPath = resolveMeshPath(QString::fromStdString(seg.mesh_path), urdfDir);
        data.meshValid = !seg.mesh_path.empty() && QFileInfo::exists(data.visual.meshPath);
        data.visual.visualOffsetMm = urdfXyzToMm(seg.origin_xyz);
        data.visual.visualRotation = urdfRpyToQuaternion(seg.origin_rpy);
        data.visual.color = seg.has_color
                                ? QColor::fromRgbF(static_cast<float>(seg.color_rgba[0]),
                                                   static_cast<float>(seg.color_rgba[1]),
                                                   static_cast<float>(seg.color_rgba[2]),
                                                   static_cast<float>(seg.color_rgba[3]))
                                : kFallbackLinkColor;
        linkToVisual.insert(linkName, data);
    }

    if (!linkToVisual.contains(baseLinkName) || !linkToVisual[baseLinkName].meshValid) {
        result.error = QStringLiteral("base link '%1' has no usable visual mesh").arg(baseLinkName);
        return result;
    }
    result.base = linkToVisual[baseLinkName].visual;

    const auto chain = model->getVisualChain();
    if (chain.empty()) {
        result.error = QStringLiteral("empty visual chain from '%1'").arg(urdfPath);
        return result;
    }

    int axisCount = 0;
    for (const auto& joint : chain) {
        if (axisCount >= UrdfVisualModel::kAxisCount) {
            break;
        }
        const QString childName = QString::fromStdString(joint.child_link);
        if (!linkToVisual.contains(childName) || !linkToVisual[childName].meshValid) {
            result.error = QStringLiteral("axis link '%1' has no usable visual mesh").arg(childName);
            return result;
        }
        UrdfAxisVisual axis;
        axis.linkName = childName;
        axis.visual = linkToVisual[childName].visual;
        axis.originMm = urdfXyzToMm(joint.origin_xyz);
        axis.restRotation = urdfRpyToQuaternion(joint.origin_rpy);
        axis.axis = QVector3D(static_cast<float>(joint.axis[0]),
                              static_cast<float>(joint.axis[1]),
                              static_cast<float>(joint.axis[2]));
        result.axes.append(axis);
        ++axisCount;
    }

    if (result.axes.size() != UrdfVisualModel::kAxisCount) {
        result.error = QStringLiteral("URDF provides %1 actuated axes, %2 required")
                           .arg(result.axes.size())
                           .arg(UrdfVisualModel::kAxisCount);
        return result;
    }

    // Compose the terminal fixed frame(s) past the actuated axes into one flange transform,
    // expressed in the last-axis frame - the same tip offset the KDL chain bakes into its tip
    // segment. A flange-less URDF leaves hasFlange false.
    if (chain.size() > static_cast<std::size_t>(UrdfVisualModel::kAxisCount)) {
        QVector3D flangePos;
        QQuaternion flangeRot;
        QString flangeLink;
        for (std::size_t i = static_cast<std::size_t>(UrdfVisualModel::kAxisCount); i < chain.size(); ++i) {
            const auto& fixedJoint = chain[i];
            const QVector3D jointPos = urdfXyzToMm(fixedJoint.origin_xyz);
            const QQuaternion jointRot = urdfRpyToQuaternion(fixedJoint.origin_rpy);
            flangePos = flangePos + flangeRot.rotatedVector(jointPos);
            flangeRot = flangeRot * jointRot;
            flangeLink = QString::fromStdString(fixedJoint.child_link);
        }
        result.hasFlange = true;
        result.flangeLinkName = flangeLink;
        result.flangeOriginMm = flangePos;
        result.flangeRestRotation = flangeRot;
        if (linkToVisual.contains(flangeLink) && linkToVisual[flangeLink].meshValid) {
            result.flangeVisual = linkToVisual[flangeLink].visual;
        }
    }

    result.valid = true;
    return result;
}

} // namespace hexa
