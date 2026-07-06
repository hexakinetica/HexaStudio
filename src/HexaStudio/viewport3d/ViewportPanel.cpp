// --- START OF FILE: HexaStudio/viewport3d/ViewportPanel.cpp ---
#include "ViewportPanel.h"

#include <QApplication>
#include <QEvent>
#include <QVBoxLayout>
#include <QtQuick/QQuickView>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQuick3D/private/qquick3dnode_p.h>
#include <QtQuick3D/private/qquick3dmodel_p.h>
#include <QtQuick3D/private/qquick3dprincipledmaterial_p.h>
#include <QtQuick3D/QQuick3DGeometry>
#include <QtQuick3DAssetUtils/private/qquick3druntimeloader_p.h>
#include <QQmlListReference>

#include <QVector3D>
#include <QQuaternion>
#include <QColor>
#include <QtMath>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QHash>
#include <QMap>
#include <QTextStream>
#include <QtEndian>
#include <QRegularExpression>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <QTimer>

#include <algorithm>
#include <limits>
#include <cstring>
#include <cmath>

#include "UrdfVisualModel.h"   // pure, unit-tested URDF -> visual-chain loader (uses robot_model)

namespace {

// URDF meshes and joint origins are authored in meters; the 3D scene works in millimeters.
constexpr float kMetersToMm = 1000.0f;
// Ghost robot is drawn slightly smaller and semi-transparent (Convention B: ghost = physical robot).
// Deliberately faint (boss review): it must read as "the physical arm is elsewhere" without shouting
// over the grey solid robot; the SHOW GHOST toggle on the UI tab hides it entirely.
// ONE GLASS DENSITY (boss consistency decision): the ghost and the executed trajectory trace share
// the same opacity step - "translucent = not the commanded NOW" - while their hues stay separate
// (steel blue = physical state, teal = plan domain), so the meanings can never be confused.
constexpr float kGhostScale = 0.992f;
constexpr float kGhostOpacity = 0.15f;   // = TrajectoryVisual::kExecutedOpacity (one glass density)
// Fixed render-only conversion from URDF/ROS Z-up to QtQuick3D Y-up. -90 deg about X maps the robot's
// +Z (up) onto the viewer's +Y (up). This is NOT a kinematic transform: the controller and all operator
// coordinates stay in the native Z-up frame.
constexpr float kZUpToYUpRxDeg = -90.0f;
const char* const kBaseLinkName = "base_link";
constexpr int kRobotAxisCount = 6;

// Display interpolation of the rendered joints (NG 0.6.16, reworked 0.6.17): status packets arrive
// at the wire cadence while the scene renders at ~60 fps, so applying them directly makes the robot
// visibly step between packets. The panel keeps the TWO latest packets per chain and replays the
// prev->curr segment LINEARLY over the following interval - constant velocity, exact arrival, one
// packet-interval display latency. (The 0.6.16 exponential low-pass was replaced: its asymptotic
// tail made every move look like it "starts fast and keeps slowing down" - boss defect report.)
// DISPLAY-ONLY: commands, feedback, the ghost-delta check and the executed-fade all keep using the
// raw controller data.
constexpr int kJointSmoothTickMs = 16;        // ~60 Hz display tick
constexpr double kJointSnapDeltaDeg = 15.0;   // larger jumps snap: no glide after a teleport/reset
constexpr double kJointSettleEpsilonDeg = 0.0005;   // below this the pose is settled - no scene churn

// Smooth-shading crease angle: face pairs sharing a vertex are smoothed only when their normals differ
// by less than this. Genuine mechanical hard edges (flanges, bores) stay sharp; curved surfaces shade
// smoothly. This is the single biggest fix for the "faceted CAD" look of raw STL.
constexpr double kCreaseAngleDeg = 45.0;

// PBR defaults for STL links shaded from their URDF colour. Matte, non-metallic response: zero
// metalness + high roughness removes the specular glints/hotspots (boss request) for a flat industrial
// painted look, while the diffuse albedo stays readable under the studio lights.
constexpr float kLinkMetalness = 0.0f;
constexpr float kLinkRoughness = 0.85f;

// Desaturated steel blue: enough hue to read "ghost", muted so it no longer glares (boss review).
const QColor kGhostColor("#5D8FC4");

QQuaternion fromURDF(float roll_deg, float pitch_deg, float yaw_deg) {
    const QQuaternion qRoll  = QQuaternion::fromAxisAndAngle(1.0f, 0.0f, 0.0f, roll_deg);
    const QQuaternion qPitch = QQuaternion::fromAxisAndAngle(0.0f, 1.0f, 0.0f, pitch_deg);
    const QQuaternion qYaw   = QQuaternion::fromAxisAndAngle(0.0f, 0.0f, 1.0f, yaw_deg);
    return qYaw * qPitch * qRoll;
}

constexpr double kPi = 3.14159265358979323846;

// Welding key: a quantized vertex position, so shared STL corners hash to one bucket.
struct VKey {
    int x = 0;
    int y = 0;
    int z = 0;
    bool operator==(const VKey& o) const { return x == o.x && y == o.y && z == o.z; }
};

inline size_t qHash(const VKey& k, size_t seed = 0) noexcept {
    return qHashMulti(seed, k.x, k.y, k.z);
}

VKey quantizeVertex(const QVector3D& p) {
    // 0.01 mm grid (positions are in meters); fine enough to weld shared vertices without merging
    // genuinely distinct ones.
    constexpr float kGrid = 100000.0f;
    return VKey{qRound(p.x() * kGrid), qRound(p.y() * kGrid), qRound(p.z() * kGrid)};
}

// Machined-metal visualization colour from the raw URDF colour: keep the authored hue but lift a very
// dark albedo (this robot's URDF is near-black, 0.098) to a visible gunmetal and desaturate slightly,
// so the arm reads as brushed metal under the studio lights instead of a featureless black blob.
QColor readableMetalColor(const QColor& urdfColor) {
    float h = 0.0f, s = 0.0f, v = 0.0f, a = 1.0f;
    urdfColor.getHsvF(&h, &s, &v, &a);
    if (h < 0.0f) {          // undefined hue for pure greys
        h = 0.0f;
        s = 0.0f;
    }
    v = std::max(v, 0.45f);
    s = std::min(s, 0.45f);
    QColor out;
    out.setHsvF(h, s, v, a);
    return out;
}

bool isStlFilePath(const QString& meshPath) {
    return meshPath.endsWith(".stl", Qt::CaseInsensitive);
}

void setSingleMaterial(QQuick3DModel* model, QQuick3DMaterial* material) {
    if (!model || !material) {
        return;
    }
    QQmlListReference materials(model, "materials");
    if (!materials.isValid()) {
        return;
    }
    if (materials.canClear()) {
        materials.clear();
    }
    if (materials.canAppend()) {
        materials.append(material);
    }
}

// TCP marker at the robot tip: a teal dot plus an XYZ triad (X red, Y green, Z blue - the universal
// robot-frame convention), parented to the tip node so it rides the flange with the arm. Unlit, so it
// stays readable regardless of the studio lighting.
//
// DISPLAY-ONLY frame correction (boss order, 2026-07-06): this URDF authors the flange frame with
// +Z pointing back INTO the arm (see the Joint_Flange comment in the URDF). Fixing it at the source
// re-interprets every stored Cartesian pose and awaits a coordinated migration - so the MARKER frame
// alone is rotated pi about X here (right-handed: Z exits the mounting face as the tool-approach
// direction, Y flips with it). Deliberate, documented discrepancy until the migration lands:
// TOOL-frame jog Z+ still follows the kinematic frame (into the arm).
// Returns the marker root so the panel can toggle it (SHOW TCP FRAME).
QQuick3DNode* attachTcpMarker(QQuick3DNode* tipNode) {
    constexpr float kSpherePrimitiveDiameter = 100.0f;
    constexpr float kCylinderPrimitiveHeight = 100.0f;
    constexpr float kCylinderPrimitiveDiameter = 100.0f;
    constexpr float kTcpDotDiameterMm = 16.0f;
    constexpr float kAxisLengthMm = 55.0f;
    constexpr float kAxisThicknessMm = 3.0f;
    constexpr float kTriadIsoPreviewRxDeg = 180.0f;   // marker-frame flip to the ISO 9787 convention

    auto* triadRoot = new QQuick3DNode(tipNode);
    triadRoot->setRotation(QQuaternion::fromAxisAndAngle(1.0f, 0.0f, 0.0f, kTriadIsoPreviewRxDeg));

    auto addUnlitModel = [triadRoot](const QString& source, const QColor& color) {
        auto* model = new QQuick3DModel(triadRoot);
        model->setSource(QUrl(source));
        auto* mat = new QQuick3DPrincipledMaterial(model);
        mat->setLighting(QQuick3DPrincipledMaterial::NoLighting);
        mat->setBaseColor(color);
        setSingleMaterial(model, mat);
        return model;
    };

    QQuick3DModel* dot = addUnlitModel(QStringLiteral("#Sphere"), QColor("#00E5CC"));
    const float dotScale = kTcpDotDiameterMm / kSpherePrimitiveDiameter;
    dot->setScale(QVector3D(dotScale, dotScale, dotScale));

    struct AxisArrow {
        QVector3D direction;
        QColor color;
    };
    const AxisArrow arrows[3] = {
        {QVector3D(1.0f, 0.0f, 0.0f), QColor("#FF5252")},   // X
        {QVector3D(0.0f, 1.0f, 0.0f), QColor("#4CAF50")},   // Y
        {QVector3D(0.0f, 0.0f, 1.0f), QColor("#2979FF")},   // Z
    };
    for (const AxisArrow& arrow : arrows) {
        QQuick3DModel* shaft = addUnlitModel(QStringLiteral("#Cylinder"), arrow.color);
        // The built-in cylinder is aligned along +Y; rotate onto the axis and shift by half a length.
        shaft->setRotation(QQuaternion::rotationTo(QVector3D(0.0f, 1.0f, 0.0f), arrow.direction));
        shaft->setScale(QVector3D(kAxisThicknessMm / kCylinderPrimitiveDiameter,
                                  kAxisLengthMm / kCylinderPrimitiveHeight,
                                  kAxisThicknessMm / kCylinderPrimitiveDiameter));
        shaft->setPosition(arrow.direction * (kAxisLengthMm * 0.5f));
    }
    return triadRoot;
}

// -----------------------------------------------------------------------------------------------
// SmoothStlGeometry: parse an STL (binary or ASCII) and emit SMOOTH per-vertex normals with a crease
// angle, so the robot no longer looks faceted. Position + normal interleaved (6 floats/vertex).
// -----------------------------------------------------------------------------------------------
class SmoothStlGeometry : public QQuick3DGeometry {
public:
    explicit SmoothStlGeometry(QQuick3DObject* parent = nullptr) : QQuick3DGeometry(parent) {}

    bool loadFromFile(const QString& meshPath) {
        clear();

        QFile file(meshPath);
        if (!file.open(QIODevice::ReadOnly)) {
            return false;
        }
        const QByteArray bytes = file.readAll();
        if (bytes.isEmpty()) {
            return false;
        }

        QVector<Triangle> triangles;
        triangles.reserve(4096);
        if (!parseBinary(bytes, triangles) && !parseAscii(bytes, triangles)) {
            return false;
        }
        if (triangles.isEmpty()) {
            return false;
        }

        return buildSmoothMesh(triangles);
    }

private:
    struct Triangle {
        QVector3D a;
        QVector3D b;
        QVector3D c;
        QVector3D weighted_normal;   // un-normalized cross (area-weighted) for accumulation
        QVector3D unit_normal;       // normalized, for the crease-angle test
    };

    static void appendTriangle(QVector<Triangle>& tris, const QVector3D& a, const QVector3D& b, const QVector3D& c) {
        QVector3D weighted = QVector3D::crossProduct(b - a, c - a);
        QVector3D unit = weighted;
        if (unit.lengthSquared() < 1e-20f) {
            unit = QVector3D(0.0f, 0.0f, 1.0f);   // degenerate triangle: harmless placeholder
        } else {
            unit.normalize();
        }
        tris.push_back({a, b, c, weighted, unit});
    }

    static bool parseBinary(const QByteArray& bytes, QVector<Triangle>& tris) {
        if (bytes.size() < 84) {
            return false;
        }
        const uchar* ptr = reinterpret_cast<const uchar*>(bytes.constData());
        const quint32 triCount = qFromLittleEndian<quint32>(ptr + 80);
        const quint64 expectedSize = 84ull + static_cast<quint64>(triCount) * 50ull;
        if (expectedSize != static_cast<quint64>(bytes.size())) {
            return false;
        }
        auto readFloatLE = [](const uchar* p) -> float {
            const quint32 word = qFromLittleEndian<quint32>(p);
            float value = 0.0f;
            std::memcpy(&value, &word, sizeof(float));
            return value;
        };
        int offset = 84;
        for (quint32 i = 0; i < triCount; ++i) {
            const QVector3D v0(readFloatLE(ptr + offset + 12), readFloatLE(ptr + offset + 16), readFloatLE(ptr + offset + 20));
            const QVector3D v1(readFloatLE(ptr + offset + 24), readFloatLE(ptr + offset + 28), readFloatLE(ptr + offset + 32));
            const QVector3D v2(readFloatLE(ptr + offset + 36), readFloatLE(ptr + offset + 40), readFloatLE(ptr + offset + 44));
            appendTriangle(tris, v0, v1, v2);
            offset += 50;
        }
        return !tris.isEmpty();
    }

    static bool parseAscii(const QByteArray& bytes, QVector<Triangle>& tris) {
        QTextStream stream(bytes);
        QVector<QVector3D> verts;
        verts.reserve(3);

        auto parseTail = [](const QStringList& parts, QVector3D& out) -> bool {
            if (parts.size() < 3) {
                return false;
            }
            bool okX = false, okY = false, okZ = false;
            const float x = parts[parts.size() - 3].toFloat(&okX);
            const float y = parts[parts.size() - 2].toFloat(&okY);
            const float z = parts[parts.size() - 1].toFloat(&okZ);
            if (!okX || !okY || !okZ) {
                return false;
            }
            out = QVector3D(x, y, z);
            return true;
        };

        while (!stream.atEnd()) {
            const QString line = stream.readLine().trimmed();
            if (line.startsWith("vertex", Qt::CaseInsensitive)) {
                QVector3D v;
                if (parseTail(line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts), v)) {
                    verts.push_back(v);
                }
            } else if (line.startsWith("endfacet", Qt::CaseInsensitive)) {
                for (int i = 1; i + 1 < verts.size(); ++i) {
                    appendTriangle(tris, verts[0], verts[i], verts[i + 1]);
                }
                verts.clear();
            }
        }
        return !tris.isEmpty();
    }

    bool buildSmoothMesh(const QVector<Triangle>& tris) {
        // Weld vertices by quantized position, so shared corners can average their faces' normals.
        QHash<VKey, QVector<int>> posToFaces;
        posToFaces.reserve(tris.size() * 3);
        for (int i = 0; i < tris.size(); ++i) {
            posToFaces[quantizeVertex(tris[i].a)].push_back(i);
            posToFaces[quantizeVertex(tris[i].b)].push_back(i);
            posToFaces[quantizeVertex(tris[i].c)].push_back(i);
        }

        const float cosCrease = static_cast<float>(std::cos(kCreaseAngleDeg * kPi / 180.0));

        auto smoothNormal = [&](const QVector3D& corner, const QVector3D& faceUnit) -> QVector3D {
            QVector3D accum(0.0f, 0.0f, 0.0f);
            const QVector<int>& faces = posToFaces.value(quantizeVertex(corner));
            for (int f : faces) {
                // Smooth only across faces within the crease angle of this face; keep hard edges sharp.
                if (QVector3D::dotProduct(tris[f].unit_normal, faceUnit) >= cosCrease) {
                    accum += tris[f].weighted_normal;   // area-weighted average
                }
            }
            if (accum.lengthSquared() < 1e-20f) {
                return faceUnit;
            }
            accum.normalize();
            return accum;
        };

        QByteArray vertexData;
        vertexData.reserve(tris.size() * 3 * 6 * static_cast<int>(sizeof(float)));

        QVector3D minBound(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
        QVector3D maxBound(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());

        auto appendVertex = [&](const QVector3D& v, const QVector3D& n) {
            const float packed[6] = {v.x(), v.y(), v.z(), n.x(), n.y(), n.z()};
            vertexData.append(reinterpret_cast<const char*>(packed), static_cast<int>(sizeof(packed)));
            minBound.setX(std::min(minBound.x(), v.x()));
            minBound.setY(std::min(minBound.y(), v.y()));
            minBound.setZ(std::min(minBound.z(), v.z()));
            maxBound.setX(std::max(maxBound.x(), v.x()));
            maxBound.setY(std::max(maxBound.y(), v.y()));
            maxBound.setZ(std::max(maxBound.z(), v.z()));
        };

        for (const Triangle& t : tris) {
            appendVertex(t.a, smoothNormal(t.a, t.unit_normal));
            appendVertex(t.b, smoothNormal(t.b, t.unit_normal));
            appendVertex(t.c, smoothNormal(t.c, t.unit_normal));
        }
        if (vertexData.isEmpty()) {
            return false;
        }

        setVertexData(vertexData);
        setStride(6 * sizeof(float));
        setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
        addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, QQuick3DGeometry::Attribute::F32Type);
        addAttribute(QQuick3DGeometry::Attribute::NormalSemantic, 3 * sizeof(float), QQuick3DGeometry::Attribute::F32Type);
        setBounds(minBound, maxBound);
        return true;
    }
};

// -----------------------------------------------------------------------------------------------
// (Custom Lines/Points trajectory geometry was removed: it does not render on the target AMD driver.
// TrajectoryVisual below draws the path with built-in sphere markers instead.)
// -----------------------------------------------------------------------------------------------

} // namespace

namespace hexa {

// -----------------------------------------------------------------------------------------------
// TrajectoryVisual: renders the trajectory preview - waypoint spheres plus a thin segment line along
// the dense planner path, with a separately-coloured (and hideable) approach/departure layer, a
// draw-in animation on arrival and a soft wipe-to-trace on executed portions. Custom Lines/Points
// geometry does NOT render on the target AMD driver, so built-in spheres/cylinders are used; the
// segment count is capped by sub-sampling so it stays bounded regardless of path length. Non-QObject
// helper; the scene nodes are owned by their Qt parent.
// -----------------------------------------------------------------------------------------------
class TrajectoryVisual {
public:
    explicit TrajectoryVisual(QQuick3DNode* parent) {
        m_root = new QQuick3DNode(parent);

        // Execution comet: bright head + soft halo, positioned on the erase front while a program
        // runs. Created once, hidden until execution is active and progress exists.
        m_cometNode = new QQuick3DNode(m_root);
        auto addCometSphere = [this](float diameterMm, const QColor& color, float opacity) {
            auto* model = new QQuick3DModel(m_cometNode);
            model->setSource(QUrl(QStringLiteral("#Sphere")));
            const float s = diameterMm / kSpherePrimitiveDiameter;
            model->setScale(QVector3D(s, s, s));
            auto* mat = new QQuick3DPrincipledMaterial(model);
            mat->setLighting(QQuick3DPrincipledMaterial::NoLighting);
            mat->setBaseColor(color);
            mat->setOpacity(opacity);
            setSingleMaterial(model, mat);
        };
        addCometSphere(kCometDiameterMm, kCometColor, 1.0f);
        addCometSphere(kCometHaloDiameterMm, kLineColor, kCometHaloOpacity);
        m_cometNode->setVisible(false);
    }

    /// @brief Program-run gate for the execution comet: the bright head on the erase front is shown
    /// only while a program actually executes (fed from HmiProgramStatus.isRunning by the panel).
    void setExecutionActive(bool active) {
        m_executionActive = active;
        updateCometState();
    }

    // Renders the trajectory as WAYPOINT markers (spheres) plus a THIN LINE (built-in cylinders, one
    // per segment - the Lines primitive does not render on the target driver). The line follows the
    // DENSE data.path when supplied: that is the true TCP path, which is a CURVE for joint-space
    // (PTP/JOINT) segments and a straight run for LIN - drawing chords between sparse waypoints made
    // the robot visibly leave the displayed line between them. Falls back to waypoint chords when no
    // dense path is provided. The segment count is bounded by sub-sampling.
    // @p animateDrawIn plays the draw-in sweep - reserved for genuinely NEW trajectory data. A mere
    // visibility toggle (SHOW APPROACH) rebuilds silently: replaying the draw-in on a toggle read as
    // "the trajectory is being re-planned" to the operator (boss review).
    void setData(const HmiTrajectoryData& data, bool approachVisible, bool animateDrawIn) {
        clearMarkers();

        const QVector<QVector3D>& lineSource = (data.path.size() >= 2) ? data.path : data.waypoints;
        const int n = static_cast<int>(lineSource.size());

        // Execution-progress state (fade-executed feature): a fresh trajectory starts fully bright.
        m_pathPoints = lineSource;
        m_progressIndex = -1;

        // Waypoint -> dense-path binding, IN PROGRAM ORDER: each waypoint's pass index is searched
        // from the previous waypoint's index onward, so a path that revisits a position (closed
        // loops, stacked passes) binds each visit to the right waypoint. The binding drives both the
        // approach/departure classification and the executed-fade trigger of the waypoint markers.
        QVector<int> waypointPathIndex;
        if (n >= 2 && !data.waypoints.isEmpty()) {
            waypointPathIndex.reserve(data.waypoints.size());
            int cursor = 0;
            for (const QVector3D& wp : data.waypoints) {
                const int idx = passIndexFrom(lineSource, wp, cursor);
                waypointPathIndex.append(idx);
                cursor = std::max(cursor, idx);
            }
        }

        // Approach / departure spans: the run-in from the current pose to the FIRST programmed
        // waypoint and the run-out past the LAST one. They are transfer moves, not the programmed
        // path, so they draw in the secondary steel colour - and are omitted entirely when the
        // operator turns SHOW APPROACH off. Only meaningful on a dense path with waypoints.
        int approachEnd = -1;
        int departureBegin = n;
        if (data.path.size() >= 2 && !waypointPathIndex.isEmpty()) {
            approachEnd = waypointPathIndex.first();
            departureBegin = waypointPathIndex.last();
        }

        const float posScale = (n > 1) ? 1.0f / static_cast<float>(n - 1) : 0.0f;
        const int step = std::max(1, (n + kMaxLineSegments - 1) / kMaxLineSegments);
        for (int i = 0; i < n - 1; i += step) {
            const int j = std::min(i + step, n - 1);
            const bool isTransferMove = (i < approachEnd) || (i >= departureBegin);
            if (isTransferMove && !approachVisible) {
                continue;
            }
            const QColor& color = isTransferMove ? kApproachColor : kLineColor;
            // Only the programmed path glows; transfer moves stay quiet secondary lines.
            addLineSegment(lineSource[i], lineSource[j], color, kLineThicknessMm, j,
                           static_cast<float>(j) * posScale, !isTransferMove);
        }

        for (int k = 0; k < data.waypoints.size(); ++k) {
            // The waypoint is "passed" once execution reaches its bound dense-path index.
            const int boundIndex = std::max(0, waypointPathIndex.value(k, 0));
            addMarker(data.waypoints[k], kWaypointColor, kWaypointDiameterMm, boundIndex,
                      static_cast<float>(boundIndex) * posScale);
        }

        // A NEW preview draws in from its start point (path order) instead of popping into view; a
        // rebuild for a visibility toggle shows instantly (visuals are constructed at full opacity).
        if (animateDrawIn) {
            startDrawInAnimation();
        }
    }

    void setVisible(bool visible) { m_root->setVisible(visible); }

    /** @brief Restores the whole trajectory to full brightness (new run / new trajectory). */
    void resetExecutionProgress() {
        if (m_progressIndex == -1) {
            return;
        }
        m_progressIndex = -1;
        applyProgressMaterials();
        updateCometState();
    }

    /**
     * @brief Advances the executed-portion fade from the robot's actual TCP (world frame, mm).
     *
     * Executed parts of the preview dim so the operator sees at a glance what remains. Progress is
     * MONOTONIC (it never walks back) and advances only when the TCP comes within
     * kProgressCaptureRadiusMm of a path point inside a bounded look-ahead window — so a path that
     * loops close to itself (the demo stacks passes a few mm apart) cannot fade future sections
     * early, and a paused robot holds the fade where it stopped.
     */
    void updateExecutionProgress(const QVector3D& tcpMm) {
        if (m_pathPoints.size() < 2) {
            return;
        }
        if (m_progressIndex < 0) {
            // (Re-)attach: after a reset the robot may be ANYWHERE on the path - e.g. a run edge
            // detected mid-program around a pause transition. Scan the WHOLE path for the FIRST
            // point within the capture radius (earliest-pass semantics); without this the bounded
            // look-ahead below can never reach a mid-path robot and the wipe freezes (boss defect
            // report: "erase stops working after a pause").
            for (int i = 0; i < m_pathPoints.size(); ++i) {
                if ((m_pathPoints[i] - tcpMm).length() < kProgressCaptureRadiusMm) {
                    m_progressIndex = i;
                    applyProgressMaterials();
                    updateCometState();
                    return;
                }
            }
            return;
        }
        const int begin = m_progressIndex + 1;
        const int end = std::min(static_cast<int>(m_pathPoints.size()), begin + kProgressSearchWindow);
        float best = kProgressCaptureRadiusMm;
        int bestIdx = -1;
        for (int i = begin; i < end; ++i) {
            const float d = (m_pathPoints[i] - tcpMm).length();
            if (d < best) {
                best = d;
                bestIdx = i;
            }
        }
        if (bestIdx > m_progressIndex) {
            m_progressIndex = bestIdx;
            applyProgressMaterials();
            updateCometState();
        }
    }

private:
    // Built-in primitive sizes: "#Sphere" is 100 units in diameter; "#Cylinder" is 100 tall / 100 wide.
    static constexpr float kSpherePrimitiveDiameter = 100.0f;
    static constexpr float kCylinderPrimitiveHeight = 100.0f;
    static constexpr float kCylinderPrimitiveDiameter = 100.0f;
    static constexpr float kWaypointDiameterMm = 18.0f;
    static constexpr float kLineThicknessMm = 4.0f;
    static constexpr int kMaxLineSegments = 160;
    // Signature cool palette on the dark studio background (boss direction: investor-grade look).
    // The path carries the PRODUCT ACCENT - the HexaTheme teal family (#57D9CE), a step brighter
    // because the filmic tonemap mutes flat unlit colours. Waypoints are platinum studs; the
    // approach/departure transfer moves are quiet graphite. Everything cool on purpose: the ghost
    // and TCP marker are already in this family, and the UI theme reserves WARM colours for
    // warnings only - so nothing in the scene shouts unless something is actually wrong.
    inline static const QColor kWaypointColor{QStringLiteral("#F4F7FB")};
    inline static const QColor kLineColor{QStringLiteral("#4FE8D2")};
    inline static const QColor kApproachColor{QStringLiteral("#64788C")};
    // Fade-executed feature: executed parts stay as a GLASS TRACE - the teal keeps its hue, dimmed
    // and nearly transparent, so the finished path reads as an elegant residue, not clutter.
    // The value matches kGhostOpacity: one shared glass density for everything that is "not the
    // commanded NOW" (boss consistency decision); hue still separates state (blue) from plan (teal).
    static constexpr float kExecutedOpacity = 0.15f;
    static constexpr int kExecutedColorDarker = 200;      // QColor::darker() factor (percent)
    static constexpr int kProgressSearchWindow = 40;      // dense points scanned ahead per update
    static constexpr float kProgressCaptureRadiusMm = 25.0f;
    // Preview lifecycle animations (boss request): a new trajectory DRAWS IN from its start point
    // instead of popping, and executed portions WIPE softly to the ghosted trace instead of
    // snapping. Plain opacity/colour animations only - shader effects are off-limits on the target
    // AMD driver (see the scene environment notes).
    static constexpr int kDrawInDurationMs = 900;
    static constexpr float kDrawInFeather = 0.18f;   // normalized width of the draw-in soft front
    static constexpr int kEraseFadeMs = 320;
    // Neon-glow line (investor-look batch, boss approved): every PROGRAMMED path segment carries a
    // second, wider translucent cylinder under the crisp core, so the path reads as a light beam.
    // Transfer moves stay quiet (no halo); executed portions drop the halo entirely - the glass
    // trace keeps only the core line. Same primitive/material class as everything else in this
    // scene: NO new GPU features (hardware-compatibility mandate - must not crash weak drivers).
    static constexpr float kGlowThicknessMm = 13.0f;
    static constexpr float kGlowOpacity = 0.20f;
    // Execution comet: a bright head riding the erase front while a program runs - the robot
    // visibly "draws with light" and the wipe becomes its trail. Hidden when no program runs.
    static constexpr float kCometDiameterMm = 12.0f;
    static constexpr float kCometHaloDiameterMm = 28.0f;
    static constexpr float kCometHaloOpacity = 0.25f;
    inline static const QColor kCometColor{QStringLiteral("#D9FFF6")};

    // First dense-path index at/after @p from where the path passes @p target: the closest approach
    // over the remaining suffix, widened by a small capture tolerance so sampling jitter cannot skip
    // the true pass. Called with a monotonic cursor (program order), which is what keeps repeated
    // positions - closed loops, stacked passes - bound to the right visit; a single global
    // nearest-index search would mis-classify a whole closed loop as transfer moves. The tolerance is
    // well below the distance between separate passes (the demo stacks passes 20 mm apart). Linear
    // scans over a bounded path, runs only at setData.
    static constexpr float kPassCaptureToleranceMm = 2.0f;
    static int passIndexFrom(const QVector<QVector3D>& path, const QVector3D& target, int from) {
        const int n = static_cast<int>(path.size());
        if (from < 0 || from >= n) {
            return -1;
        }
        float minDist = std::numeric_limits<float>::max();
        for (int i = from; i < n; ++i) {
            minDist = std::min(minDist, (path[i] - target).length());
        }
        const float capture = minDist + kPassCaptureToleranceMm;
        for (int i = from; i < n; ++i) {
            if ((path[i] - target).length() <= capture) {
                return i;
            }
        }
        return -1;   // unreachable: the suffix is non-empty, so some index is within capture
    }

    // One rendered visual with the path index that marks it as executed once progress passes it,
    // plus its normalized position along the path (drives the draw-in front) and its animation state.
    // Opacity is per-visual: a glow halo is never fully opaque (full = kGlowOpacity) and vanishes
    // once executed (executed = 0), while core visuals go 1.0 -> kExecutedOpacity.
    struct FadingVisual {
        QQuick3DModel* model = nullptr;
        QQuick3DPrincipledMaterial* material = nullptr;
        QColor base_color;
        int executed_at_index = 0;
        float path_pos = 0.0f;                    // 0..1 along the dense path
        bool shown_executed = false;              // last state applied to the material
        QVariantAnimation* fade_anim = nullptr;   // running wipe animation (owned by the material)
        float full_opacity = 1.0f;                // opacity when bright (pending execution)
        float executed_opacity = 0.15f;           // opacity once executed (the glass trace)
    };

    void addMarker(const QVector3D& positionMm, const QColor& color, float diameterMm, int pathIndex,
                   float pathPos) {
        auto* model = new QQuick3DModel(m_root);
        model->setSource(QUrl(QStringLiteral("#Sphere")));
        const float s = diameterMm / kSpherePrimitiveDiameter;
        model->setScale(QVector3D(s, s, s));
        model->setPosition(positionMm);
        auto* mat = new QQuick3DPrincipledMaterial(model);
        mat->setLighting(QQuick3DPrincipledMaterial::NoLighting);
        mat->setBaseColor(color);
        setSingleMaterial(model, mat);
        m_markers.append(model);
        m_fadingVisuals.append({model, mat, color, pathIndex, pathPos, false, nullptr,
                                1.0f, kExecutedOpacity});
    }

    // Dim executed visuals (soft wipe), restore pending ones (instant). Runs only when the progress
    // index changes; only visuals whose executed state CHANGED are touched, so a pass stays cheap
    // and a wipe animation is started exactly once per visual (progress is monotonic).
    void applyProgressMaterials() {
        // Execution state now owns the materials: a still-running draw-in must complete first, or
        // the segments past its front would be left invisible.
        finishDrawInAnimation();
        for (FadingVisual& v : m_fadingVisuals) {
            if (!v.material) {
                continue;
            }
            const bool executed = v.executed_at_index <= m_progressIndex;
            if (executed == v.shown_executed) {
                continue;
            }
            v.shown_executed = executed;
            if (executed) {
                // The passed portion wipes to the ghosted trace instead of snapping (boss request).
                startEraseAnimation(v, v.base_color.darker(kExecutedColorDarker), v.executed_opacity);
            } else {
                // Restore (new run): instant full brightness; kill a still-running wipe first so it
                // cannot overwrite the restored values on its next tick.
                stopEraseAnimation(v);
                v.material->setBaseColor(v.base_color);
                v.material->setOpacity(v.full_opacity);
            }
        }
    }

    void stopEraseAnimation(FadingVisual& v) {
        if (v.fade_anim) {
            v.fade_anim->stop();
            v.fade_anim->deleteLater();
            v.fade_anim = nullptr;
        }
    }

    // Animates the material from its CURRENT colour/opacity to the given target (the erase wipe).
    // One bounded QVariantAnimation per newly-executed visual, owned by the material (dies with it).
    void startEraseAnimation(FadingVisual& v, const QColor& targetColor, float targetOpacity) {
        stopEraseAnimation(v);
        QQuick3DPrincipledMaterial* mat = v.material;
        const QColor fromColor = mat->baseColor();
        const float fromOpacity = mat->opacity();
        auto* anim = new QVariantAnimation(mat);
        anim->setStartValue(0.0);
        anim->setEndValue(1.0);
        anim->setDuration(kEraseFadeMs);
        anim->setEasingCurve(QEasingCurve::InOutQuad);
        QObject::connect(anim, &QVariantAnimation::valueChanged, mat,
                         [mat, fromColor, targetColor, fromOpacity, targetOpacity](const QVariant& value) {
            const float t = static_cast<float>(value.toDouble());
            const auto lerp = [t](float a, float b) { return a + (b - a) * t; };
            QColor c;
            c.setRgbF(lerp(fromColor.redF(), targetColor.redF()),
                      lerp(fromColor.greenF(), targetColor.greenF()),
                      lerp(fromColor.blueF(), targetColor.blueF()));
            mat->setBaseColor(c);
            mat->setOpacity(lerp(fromOpacity, targetOpacity));
        });
        v.fade_anim = anim;
        anim->start();
    }

    // ---- Draw-in: a freshly displayed trajectory materializes from its start point. -------------
    // One shared animation sweeps a soft front (kDrawInFeather wide) along the normalized path
    // positions; each visual's opacity follows the front. Bounded work per tick (<= visual count).

    void startDrawInAnimation() {
        cancelDrawInAnimation();
        if (m_fadingVisuals.isEmpty()) {
            return;
        }
        for (const FadingVisual& v : m_fadingVisuals) {
            if (v.material) {
                v.material->setOpacity(0.0f);
            }
        }
        auto* anim = new QVariantAnimation(m_root);
        anim->setStartValue(0.0);
        anim->setEndValue(1.0);
        anim->setDuration(kDrawInDurationMs);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        QObject::connect(anim, &QVariantAnimation::valueChanged, m_root, [this](const QVariant& value) {
            applyDrawInFront(static_cast<float>(value.toDouble()));
        });
        QObject::connect(anim, &QVariantAnimation::finished, m_root, [this]() {
            finishDrawInAnimation();
        });
        m_drawInAnim = anim;
        anim->start();
    }

    void applyDrawInFront(float progress) {
        const float front = progress * (1.0f + kDrawInFeather);
        for (const FadingVisual& v : m_fadingVisuals) {
            if (!v.material) {
                continue;
            }
            const float alpha = std::clamp((front - v.path_pos) / kDrawInFeather, 0.0f, 1.0f);
            v.material->setOpacity(alpha * v.full_opacity);
        }
    }

    void cancelDrawInAnimation() {
        if (m_drawInAnim) {
            m_drawInAnim->stop();
            m_drawInAnim->deleteLater();
            m_drawInAnim = nullptr;
        }
    }

    // Completes the draw-in instantly (all visuals fully shown). Used when execution takes over the
    // materials and on the animation's own finish.
    void finishDrawInAnimation() {
        if (!m_drawInAnim) {
            return;
        }
        cancelDrawInAnimation();
        applyDrawInFront(1.0f);
    }

    // A thin cylinder spanning a -> b, used as one straight line segment between two waypoints.
    // @p endPathIndex is the dense-path index of point b: the segment counts as executed once the
    // execution progress passes it.
    // One straight segment a -> b. @p glow adds the neon halo pass (programmed path only): a wider
    // translucent cylinder under the crisp core, full at kGlowOpacity and gone once executed.
    void addLineSegment(const QVector3D& a, const QVector3D& b, const QColor& color, float thicknessMm,
                        int endPathIndex, float pathPos, bool glow) {
        const QVector3D delta = b - a;
        const float length = delta.length();
        if (length < 1e-3f) {
            return;
        }
        const QQuaternion rotation = QQuaternion::rotationTo(QVector3D(0.0f, 1.0f, 0.0f), delta / length);
        const QVector3D center = (a + b) * 0.5f;

        auto makeCylinder = [&](float widthMm, float fullOpacity, float executedOpacity) {
            auto* model = new QQuick3DModel(m_root);
            model->setSource(QUrl(QStringLiteral("#Cylinder")));
            model->setPosition(center);
            // The built-in cylinder is aligned along +Y; rotate that axis onto the segment direction.
            model->setRotation(rotation);
            model->setScale(QVector3D(widthMm / kCylinderPrimitiveDiameter,
                                      length / kCylinderPrimitiveHeight,
                                      widthMm / kCylinderPrimitiveDiameter));
            auto* mat = new QQuick3DPrincipledMaterial(model);
            mat->setLighting(QQuick3DPrincipledMaterial::NoLighting);
            mat->setBaseColor(color);
            mat->setOpacity(fullOpacity);
            setSingleMaterial(model, mat);
            m_markers.append(model);
            m_fadingVisuals.append({model, mat, color, endPathIndex, pathPos, false, nullptr,
                                    fullOpacity, executedOpacity});
        };

        makeCylinder(thicknessMm, 1.0f, kExecutedOpacity);
        if (glow) {
            makeCylinder(kGlowThicknessMm, kGlowOpacity, 0.0f);
        }
    }

    // Positions/hides the execution comet: visible only while a program runs AND execution progress
    // exists; rides the dense-path point the erase front has reached.
    void updateCometState() {
        if (!m_cometNode) {
            return;
        }
        const bool visible = m_executionActive && m_progressIndex >= 0
                             && m_progressIndex < m_pathPoints.size();
        if (visible) {
            m_cometNode->setPosition(m_pathPoints[m_progressIndex]);
        }
        m_cometNode->setVisible(visible);
    }

    void clearMarkers() {
        // The visuals the shared draw-in animation drives are going away with the markers.
        cancelDrawInAnimation();
        for (QQuick3DModel* m : m_markers) {
            if (m) {
                // Hide and detach IMMEDIATELY: deleteLater() only frees the object on the next event
                // loop pass, and several setData() calls can happen before that (e.g. the staged
                // screenshot sweep) - stale markers must not accumulate visibly in the scene.
                m->setVisible(false);
                m->setParentItem(nullptr);
                m->deleteLater();
            }
        }
        m_markers.clear();
        m_fadingVisuals.clear();   // materials are owned by their (deleted) models
        m_pathPoints.clear();
        m_progressIndex = -1;
        updateCometState();        // no path -> no comet
    }

    QQuick3DNode* m_root = nullptr;
    QVector<QQuick3DModel*> m_markers;
    QVector<FadingVisual> m_fadingVisuals;
    QVector<QVector3D> m_pathPoints;   ///< Dense path copy driving the executed-portion fade.
    int m_progressIndex = -1;          ///< Highest dense-path index reached by execution (-1 = none).
    QVariantAnimation* m_drawInAnim = nullptr;   ///< Shared draw-in front sweep (owned by m_root).
    QQuick3DNode* m_cometNode = nullptr;         ///< Execution comet (bright head + halo spheres).
    bool m_executionActive = false;              ///< True while a program runs (comet gate).
};

// -----------------------------------------------------------------------------------------------
// ViewportPanel
// -----------------------------------------------------------------------------------------------
ViewportPanel::ViewportPanel(QWidget* parent) : QWidget(parent) {
    // Default to the OpenGL RHI backend (other Qt 6 backends have shown depth-buffer artifacts here),
    // but respect an explicit QSG_RHI_BACKEND override so a host can pick d3d11/vulkan when needed
    // (e.g. headless screenshot rendering).
    if (qEnvironmentVariableIsEmpty("QSG_RHI_BACKEND")) {
        qputenv("QSG_RHI_BACKEND", "opengl");
    }
    setupUi();
    setup3DScene();

    // Display-smoothing tick (kJointSmoothTauS): eases the rendered joints toward the latest
    // status targets, so the wire packet cadence never shows as stepping.
    m_smoothClock.start();
    m_smoothTimer = new QTimer(this);
    m_smoothTimer->setInterval(kJointSmoothTickMs);
    m_smoothTimer->setTimerType(Qt::PreciseTimer);
    connect(m_smoothTimer, &QTimer::timeout, this, &ViewportPanel::onSmoothTick);
    m_smoothTimer->start();

    // The showroom turntable must never start while the OPERATOR is working anywhere in the HMI
    // (boss review 2026-07-07) - camera input inside the scene already re-arms the idle timer,
    // but jogging or editing in a side panel did not, so the camera started orbiting under the
    // operator's hands. An application-wide filter treats every deliberate input (click, wheel,
    // key, touch) as activity; mouse MOVES are excluded so resting a hand on the mouse does not
    // permanently disable the showroom idle behaviour.
    qApp->installEventFilter(this);
}

bool ViewportPanel::eventFilter(QObject* watched, QEvent* event) {
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
    case QEvent::Wheel:
    case QEvent::KeyPress:
    case QEvent::TouchBegin:
        notifyOperatorActivity();
        break;
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

void ViewportPanel::notifyOperatorActivity() {
    // Throttle: input bursts (typing, repeated jog taps) collapse to one QML call per 250 ms.
    const qint64 now = m_smoothClock.elapsed();
    if (now - m_lastOperatorActivityMs < 250) {
        return;
    }
    m_lastOperatorActivityMs = now;
    if (m_quickView && m_quickView->rootObject()) {
        // Same entry the scene's own camera input uses: stops an active turntable and re-arms
        // the idle timer, so "idle" means "no operator input anywhere", not just "no camera input".
        QMetaObject::invokeMethod(m_quickView->rootObject(), "notifyCameraInteraction");
    }
}

ViewportPanel::~ViewportPanel() = default;

void ViewportPanel::setupUi() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGLRhi);

    // A QQuickView hosted in a native window container. This renders the QtQuick3D scene directly to a
    // native child window instead of compositing a widget-owned texture into the parent's backing
    // store - the latter (QQuickWidget) path crashes in QRhi's backing-store flush on this driver.
    m_quickView = new QQuickView();
    m_quickView->setResizeMode(QQuickView::SizeRootObjectToView);
    m_quickView->setColor(QColor("#20242e"));
    m_quickView->setSource(QUrl("qrc:/ViewportScene.qml"));

    QWidget* container = QWidget::createWindowContainer(m_quickView, this);
    container->setMinimumSize(400, 300);
    container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    container->setFocusPolicy(Qt::StrongFocus);
    layout->addWidget(container, 1);
}

QImage ViewportPanel::grabViewport() {
    return m_quickView ? m_quickView->grabWindow() : QImage();
}

void ViewportPanel::setup3DScene() {
    QQuickItem* rootObject = m_quickView->rootObject();
    if (!rootObject) {
        qWarning() << "ViewportPanel: failed to load QML scene (qrc:/ViewportScene.qml).";
        return;
    }
    m_sceneRoot = rootObject->property("sceneRoot").value<QQuick3DNode*>();
    if (!m_sceneRoot) {
        qWarning() << "ViewportPanel: sceneRoot object not found in QML.";
        return;
    }

    // Fixed Z-up (URDF) -> Y-up (QtQuick3D) render conversion. All world-frame content is parented here.
    m_worldRoot = new QQuick3DNode(m_sceneRoot);
    m_worldRoot->setRotation(fromURDF(kZUpToYUpRxDeg, 0.0f, 0.0f));

    rebuildRobotChains();

    // Trajectory + TCP live in the Z-up world frame (under m_worldRoot), the same frame the robot
    // renders in after its URDF correction, so they stay aligned with it.
    m_trajectory = std::make_unique<TrajectoryVisual>(m_worldRoot);

    // TCP marker: a small green sphere at the FK tip (world-frame feedback), shown once valid.
    m_tcpMarkerEntity = new QQuick3DNode(m_worldRoot);
    auto* tcpModel = new QQuick3DModel(m_tcpMarkerEntity);
    tcpModel->setSource(QUrl("#Sphere"));
    tcpModel->setScale(QVector3D(0.1f, 0.1f, 0.1f));
    auto* tcpMat = new QQuick3DPrincipledMaterial(tcpModel);
    tcpMat->setBaseColor(QColor(Qt::green));
    tcpMat->setLighting(QQuick3DPrincipledMaterial::NoLighting);
    setSingleMaterial(tcpModel, tcpMat);
    m_tcpMarkerEntity->setVisible(false);

    updateVisualState();
}

bool ViewportPanel::setRobotModelConfig(const QString& urdfPath, double xMm, double yMm, double zMm,
                                        double rxDeg, double ryDeg, double rzDeg) {
    const QString normalizedPath = QDir::cleanPath(urdfPath);
    const QFileInfo fi(normalizedPath);
    if (normalizedPath.isEmpty() || !fi.exists() || !fi.isFile()) {
        qWarning() << "ViewportPanel: invalid robot model URDF path:" << urdfPath;
        return false;
    }
    if (fi.suffix().compare("urdf", Qt::CaseInsensitive) != 0) {
        qWarning() << "ViewportPanel: selected file is not a URDF:" << normalizedPath;
        return false;
    }

    const bool pathChanged = (normalizedPath != m_modelConfig.urdfPath);
    m_modelConfig.urdfPath = normalizedPath;
    m_modelConfig.mount_x_mm = xMm;
    m_modelConfig.mount_y_mm = yMm;
    m_modelConfig.mount_z_mm = zMm;
    m_modelConfig.mount_rx_deg = rxDeg;
    m_modelConfig.mount_ry_deg = ryDeg;
    m_modelConfig.mount_rz_deg = rzDeg;

    if (pathChanged) {
        rebuildRobotChains();   // heavy: re-parse URDF + reload meshes
    } else {
        applyRootTransform();   // light: only the mount transform moved
    }

    const bool ok = (m_mainRobot.transforms.size() == kRobotAxisCount
                     && m_ghostRobot.transforms.size() == kRobotAxisCount);
    if (!ok) {
        qWarning() << "ViewportPanel: imported model did not produce a valid 6-axis chain:" << normalizedPath;
    }
    return ok;
}

void ViewportPanel::applyRootTransformTo(QQuick3DNode* rootEntity, bool isGhost) const {
    if (!rootEntity) {
        return;
    }
    // The single world->base root transform, identical to the controller's root_transform (this is
    // what keeps visual robot, KDL FK and trajectory in one frame). The render Z-up->Y-up stays
    // separate on m_worldRoot (the parent), so the operator/controller frame is untouched.
    rootEntity->setPosition(QVector3D(static_cast<float>(m_modelConfig.mount_x_mm),
                                      static_cast<float>(m_modelConfig.mount_y_mm),
                                      static_cast<float>(m_modelConfig.mount_z_mm)));
    rootEntity->setRotation(fromURDF(static_cast<float>(m_modelConfig.mount_rx_deg),
                                     static_cast<float>(m_modelConfig.mount_ry_deg),
                                     static_cast<float>(m_modelConfig.mount_rz_deg)));
    const float scale = isGhost ? kGhostScale : 1.0f;
    rootEntity->setScale(QVector3D(scale, scale, scale));
}

void ViewportPanel::applyRootTransform() {
    applyRootTransformTo(m_mainRobot.root_entity, false);
    applyRootTransformTo(m_ghostRobot.root_entity, true);
}

bool ViewportPanel::loadRobotUrdfModel() {
    m_jointRestRotations.clear();
    m_jointAxes.clear();
    m_jointOrigins.clear();
    m_jointVisualOffsets.clear();
    m_jointVisualRotations.clear();
    m_jointMeshPaths.clear();
    m_jointLinkNames.clear();
    m_jointLinkColors.clear();
    m_baseMeshPath.clear();
    m_baseVisualOffset = QVector3D();
    m_baseVisualRotation = QQuaternion();

    m_hasFlangeFrame = false;
    m_flangeOrigin = QVector3D();
    m_flangeRestRotation = QQuaternion();
    m_flangeVisualOffset = QVector3D();
    m_flangeVisualRotation = QQuaternion();
    m_flangeMeshPath.clear();
    m_flangeLinkName.clear();

    // All parsing/resolution lives in the pure, unit-tested loader (UrdfVisualModel); this method only
    // copies the result into the panel's scene-building state.
    const UrdfVisualModel model = loadUrdfVisualModel(m_modelConfig.urdfPath, QString::fromUtf8(kBaseLinkName));
    if (!model.valid) {
        qWarning() << "ViewportPanel: URDF visual model load failed:" << model.error;
        return false;
    }

    m_baseMeshPath = model.base.meshPath;
    m_baseColor = model.base.color;
    m_baseVisualOffset = model.base.visualOffsetMm;
    m_baseVisualRotation = model.base.visualRotation;

    for (const UrdfAxisVisual& axis : model.axes) {
        m_jointLinkNames.append(axis.linkName);
        m_jointMeshPaths.append(axis.visual.meshPath);
        m_jointVisualOffsets.append(axis.visual.visualOffsetMm);
        m_jointVisualRotations.append(axis.visual.visualRotation);
        m_jointLinkColors.append(axis.visual.color);
        m_jointOrigins.append(axis.originMm);
        m_jointRestRotations.append(axis.restRotation);
        m_jointAxes.append(axis.axis);
    }

    m_hasFlangeFrame = model.hasFlange;
    if (model.hasFlange) {
        m_flangeOrigin = model.flangeOriginMm;
        m_flangeRestRotation = model.flangeRestRotation;
        m_flangeLinkName = model.flangeLinkName;
        m_flangeMeshPath = model.flangeVisual.meshPath;
        m_flangeVisualOffset = model.flangeVisual.visualOffsetMm;
        m_flangeVisualRotation = model.flangeVisual.visualRotation;
        m_flangeColor = model.flangeVisual.color;
        qInfo() << "ViewportPanel: flange frame captured for link" << m_flangeLinkName
                << "mesh:" << (m_flangeMeshPath.isEmpty() ? QStringLiteral("<none>") : m_flangeMeshPath);
    }

    return (m_jointMeshPaths.size() == kRobotAxisCount);
}

ViewportPanel::MeshVisual ViewportPanel::attachMeshVisual(QQuick3DNode* parentNode, const QString& meshPath,
                                                          const QColor& urdfColor, const QVector3D& visualOffset,
                                                          const QQuaternion& visualRot, bool isGhost,
                                                          const QString& diagName) {
    if (!parentNode) {
        qWarning() << "ViewportPanel: attachMeshVisual() null parent for" << diagName;
        return {nullptr, false};
    }

    // The ghost is always drawn from the STL as a uniform translucent silhouette (Convention B), so an
    // advanced-mesh upgrade is applied to the SOLID robot only.
    const QString resolvedPath = isGhost ? meshPath : resolvePreferredMesh(meshPath);

    if (!isGhost && !isStlFilePath(resolvedPath)) {
        // Advanced mesh (glTF/GLB/OBJ): keep the asset's own PBR materials/textures/normals.
        auto* loader = new QQuick3DRuntimeLoader(parentNode);
        loader->setPosition(visualOffset);
        loader->setRotation(visualRot);
        loader->setScale(QVector3D(kMetersToMm, kMetersToMm, kMetersToMm));
        loader->setSource(QUrl::fromLocalFile(resolvedPath));
        return {nullptr, true};   // asset self-materials; nothing to restyle
    }

    auto* visualEntity = new QQuick3DModel(parentNode);
    visualEntity->setCastsShadows(true);
    visualEntity->setReceivesShadows(!isGhost);
    visualEntity->setPosition(visualOffset);
    visualEntity->setRotation(visualRot);

    // Parsed-STL cache: the main and ghost chains use the same meshes, so each file is parsed and
    // smooth-shaded once; the geometry object (owned by m_worldRoot, outliving both chains) is shared.
    QQuick3DGeometry* geometry = m_stlGeometryCache.value(resolvedPath, nullptr);
    if (!geometry) {
        auto* loaded = new SmoothStlGeometry(m_worldRoot);
        if (!loaded->loadFromFile(resolvedPath)) {
            qWarning() << "ViewportPanel: STL parse failed for" << diagName << "path:" << resolvedPath;
            loaded->deleteLater();
            visualEntity->setVisible(false);
            return {nullptr, false};
        }
        m_stlGeometryCache.insert(resolvedPath, loaded);
        geometry = loaded;
    }
    visualEntity->setGeometry(geometry);
    visualEntity->setScale(QVector3D(kMetersToMm, kMetersToMm, kMetersToMm));

    auto* material = new QQuick3DPrincipledMaterial(visualEntity);
    if (isGhost) {
        material->setBaseColor(kGhostColor);
        material->setAlphaMode(QQuick3DPrincipledMaterial::Blend);
        material->setOpacity(kGhostOpacity);
        material->setLighting(QQuick3DPrincipledMaterial::NoLighting);
        material->setCullMode(QQuick3DPrincipledMaterial::NoCulling);
    } else {
        material->setBaseColor(readableMetalColor(urdfColor));   // REQ 5.1: authored URDF hue, lifted to a readable metal
        material->setMetalness(kLinkMetalness);
        material->setRoughness(kLinkRoughness);
    }
    setSingleMaterial(visualEntity, material);
    return {material, true};
}

ViewportPanel::RobotChain ViewportPanel::buildRobotChain(QQuick3DNode* rootParent, const QString& nameSuffix, bool isGhost) {
    RobotChain chain;
    chain.root_entity = new QQuick3DNode(rootParent);
    chain.root_entity->setObjectName("RobotRoot_" + nameSuffix);
    chain.root_entity->setProperty("chainValid", false);
    applyRootTransformTo(chain.root_entity, isGhost);

    auto* baseLinkEntity = new QQuick3DNode(chain.root_entity);
    // The base link's authored <visual><origin> must be honoured exactly like the axis links':
    // a re-anchored URDF places the base mesh through this offset, and identity here detaches it.
    const MeshVisual baseVisual = attachMeshVisual(baseLinkEntity, m_baseMeshPath, m_baseColor,
                                                   m_baseVisualOffset, m_baseVisualRotation, isGhost,
                                                   QStringLiteral("base:%1").arg(nameSuffix));
    if (!baseVisual.ok) {
        chain.root_entity->setVisible(false);
        return chain;
    }
    if (baseVisual.material) {
        chain.materials.append(baseVisual.material);
    }

    if (m_jointMeshPaths.size() < kRobotAxisCount) {
        qWarning() << "ViewportPanel: robot chain data is incomplete.";
        return chain;
    }

    QQuick3DNode* parent = baseLinkEntity;
    bool linksOk = true;
    for (int i = 0; i < kRobotAxisCount; ++i) {
        auto* jointEntity = new QQuick3DNode(parent);
        jointEntity->setObjectName(QStringLiteral("Joint_%1").arg(m_jointLinkNames.value(i, QStringLiteral("axis_%1").arg(i + 1))));
        jointEntity->setPosition(m_jointOrigins[i]);
        jointEntity->setRotation(m_jointRestRotations[i]);

        const MeshVisual link = attachMeshVisual(jointEntity, m_jointMeshPaths[i], m_jointLinkColors[i],
                                                 m_jointVisualOffsets[i], m_jointVisualRotations[i], isGhost,
                                                 m_jointLinkNames.value(i));
        chain.transforms.append(jointEntity);
        if (link.material) {
            chain.materials.append(link.material);
        }
        parent = jointEntity;
        if (!link.ok) {
            linksOk = false;
        }
    }

    if (!linksOk) {
        qWarning() << "ViewportPanel: failed to build full visual chain for" << nameSuffix;
        chain.transforms.clear();
        chain.materials.clear();
        chain.root_entity->setVisible(false);
        return chain;
    }

    // Terminal flange frame: a fixed node off the last axis - the robot's real tip and the tool/TCP
    // attach anchor. Created whenever the URDF defines a flange joint, with or without a visual mesh
    // (a bare frame is the common case). A flange-less URDF adds nothing (renders as before).
    if (m_hasFlangeFrame) {
        auto* flangeEntity = new QQuick3DNode(parent);
        flangeEntity->setObjectName(QStringLiteral("Flange_%1").arg(nameSuffix));
        flangeEntity->setPosition(m_flangeOrigin);
        flangeEntity->setRotation(m_flangeRestRotation);
        chain.flange_entity = flangeEntity;

        if (!m_flangeMeshPath.isEmpty()) {
            const MeshVisual flangeVisual = attachMeshVisual(flangeEntity, m_flangeMeshPath, m_flangeColor,
                                                             m_flangeVisualOffset, m_flangeVisualRotation, isGhost,
                                                             QStringLiteral("flange:%1").arg(m_flangeLinkName));
            if (flangeVisual.ok && flangeVisual.material) {
                chain.materials.append(flangeVisual.material);
            } else if (!flangeVisual.ok) {
                qWarning() << "ViewportPanel: flange mesh failed to load for" << nameSuffix
                           << "link:" << m_flangeLinkName << "(frame kept, mesh skipped)";
            }
        }
    }

    // TCP highlight (boss request): teal dot + XYZ triad at the tip of the SOLID robot only (the
    // ghost stays a clean silhouette). Rides the flange frame when the URDF defines one, else the
    // last actuated axis.
    if (!isGhost) {
        QQuick3DNode* tipNode = chain.flange_entity ? chain.flange_entity : parent;
        chain.tcp_marker = attachTcpMarker(tipNode);
    }

    chain.root_entity->setProperty("chainValid", true);
    return chain;
}

void ViewportPanel::clearRobotChain(RobotChain& chain) {
    if (chain.root_entity) {
        chain.root_entity->deleteLater();
        chain.root_entity = nullptr;
    }
    chain.transforms.clear();
    chain.materials.clear();
    chain.flange_entity = nullptr;
    chain.tcp_marker = nullptr;
}

void ViewportPanel::rebuildRobotChains() {
    m_isRebuildingChains = true;

    clearRobotChain(m_mainRobot);
    clearRobotChain(m_ghostRobot);

    // The mesh set may have changed with the URDF; drop the parsed-STL cache before reloading.
    for (QQuick3DGeometry* geometry : m_stlGeometryCache) {
        if (geometry) {
            geometry->deleteLater();
        }
    }
    m_stlGeometryCache.clear();

    if (!loadRobotUrdfModel()) {
        qWarning() << "ViewportPanel: failed to load URDF model; robot chain not rendered.";
        m_isRebuildingChains = false;
        return;
    }

    m_mainRobot = buildRobotChain(m_worldRoot, "MainRobot", false);
    m_ghostRobot = buildRobotChain(m_worldRoot, "GhostRobot", true);

    // Fresh chains stand at the rest pose: drop the interpolation streams so the next status
    // packet SNAPS (pushJointPacket) instead of gliding from a stale pose.
    m_mainJointStream = JointStream{};
    m_ghostJointStream = JointStream{};

    updateVisualState();
    m_isRebuildingChains = false;
}

void ViewportPanel::updateState(const HmiMotionStatus& status) {
    if (m_isRebuildingChains) {
        return;
    }

    if (status.actualTcp.size() >= 3) {
        const QVector3D tcp(static_cast<float>(status.actualTcp[0]),
                            static_cast<float>(status.actualTcp[1]),
                            static_cast<float>(status.actualTcp[2]));
        if (tcp.lengthSquared() > 1e-6f) {
            m_lastRobotTcp = tcp;
            if (m_tcpMarkerEntity) {
                m_tcpMarkerEntity->setPosition(m_lastRobotTcp);
                m_tcpMarkerEntity->setVisible(true);
            }
            // Fade-executed trajectory: advance only while a program is executing, so jogging near
            // the preview never falsely dims it.
            if (m_programRunning && m_trajectory) {
                m_trajectory->updateExecutionProgress(m_lastRobotTcp);
            }
        }
    }

    auto meaningfulDelta = [](const QVector<double>& a, const QVector<double>& b) {
        if (a.isEmpty() || b.isEmpty()) {
            return false;
        }
        const int n = std::min(a.size(), b.size());
        constexpr double kJointDeltaEps = 0.0035;   // ~0.2 deg
        for (int i = 0; i < n; ++i) {
            if (std::abs(a[i] - b[i]) > kJointDeltaEps) {
                return true;
            }
        }
        return false;
    };

    const bool newGhostDelta = meaningfulDelta(status.plannedJoints, status.realHardwareJoints);
    if (m_hasGhostDelta != newGhostDelta) {
        m_hasGhostDelta = newGhostDelta;
        updateVisualState();
    }

    // Camera automation gate: the showroom turntable must never run while the robot moves (boss
    // review - camera motion on top of robot motion is disorienting). Pushed as a QML property
    // only on change.
    const bool motionActive = status.isMoving || m_programRunning;
    if (motionActive != m_lastMotionActiveForCamera && m_quickView && m_quickView->rootObject()) {
        m_lastMotionActiveForCamera = motionActive;
        m_quickView->rootObject()->setProperty("motionActive", motionActive);
    }

    // Convention B: solid robot = commanded pose; ghost = physical robot. The raw packet values
    // feed the display interpolation streams; onSmoothTick replays them so the packet cadence
    // never shows as stepping. The ghost is interpolated even while hidden (trivial cost), so
    // re-enabling it never pops a stale pose.
    pushJointPacket(status.plannedJoints, m_mainJointStream, m_mainRobot);
    pushJointPacket(status.realHardwareJoints, m_ghostJointStream, m_ghostRobot);

    // TEMPORARY DIAGNOSTIC (progressive-slowdown investigation, 0.6.18 - REMOVE when closed):
    // once per 5 s log the status cadence and the mean joint step per pose CHANGE. During a run:
    //   - change rate steady, step shrinking  -> the CONTROLLER is genuinely decelerating;
    //   - change rate dropping                -> transport/backlog is starving the display;
    //   - both steady while the scene slows   -> display-side math, dig here.
    m_diagPackets++;
    if (m_diagPrevJoints.size() == status.plannedJoints.size() && !m_diagPrevJoints.isEmpty()) {
        double maxStepDeg = 0.0;
        for (int i = 0; i < status.plannedJoints.size(); ++i) {
            maxStepDeg = std::max(maxStepDeg, std::abs(status.plannedJoints[i] - m_diagPrevJoints[i]));
        }
        if (maxStepDeg > 1e-9) {
            m_diagPoseChanges++;
            m_diagStepSumDeg += maxStepDeg;
        }
    }
    m_diagPrevJoints = status.plannedJoints;
    const qint64 diagNowMs = m_smoothClock.elapsed();
    if (m_diagWindowStartMs == 0) {
        m_diagWindowStartMs = diagNowMs;
    } else if (diagNowMs - m_diagWindowStartMs >= 5000) {
        qInfo().nospace() << "ViewportPanel[diag]: " << m_diagPackets << " status pkts / "
                          << (diagNowMs - m_diagWindowStartMs) << " ms, pose changes "
                          << m_diagPoseChanges << ", mean joint step "
                          << (m_diagPoseChanges > 0 ? m_diagStepSumDeg / m_diagPoseChanges : 0.0)
                          << " deg";
        m_diagPackets = 0;
        m_diagPoseChanges = 0;
        m_diagStepSumDeg = 0.0;
        m_diagWindowStartMs = diagNowMs;
    }
}

void ViewportPanel::applyJointsToChain(const QVector<double>& joints, RobotChain& robot) {
    const int count = std::min({static_cast<int>(joints.size()), static_cast<int>(robot.transforms.size()),
                                static_cast<int>(m_jointRestRotations.size()), static_cast<int>(m_jointAxes.size())});
    for (int i = 0; i < count; ++i) {
        if (!robot.transforms[i]) {
            continue;
        }
        const float angle = static_cast<float>(joints[i]);
        const QVector3D localAxis = m_jointAxes[i].normalized();
        const QVector3D axisInParent = m_jointRestRotations[i].rotatedVector(localAxis);
        const QQuaternion jointRot = QQuaternion::fromAxisAndAngle(axisInParent, angle);
        robot.transforms[i]->setRotation(jointRot * m_jointRestRotations[i]);
    }
}

void ViewportPanel::pushJointPacket(const QVector<double>& joints, JointStream& stream,
                                    RobotChain& robot) {
    if (joints.isEmpty()) {
        return;
    }
    const qint64 nowMs = m_smoothClock.elapsed();
    // First data after start/rebuild, or a teleport-sized jump (program load, sim reset): SNAP.
    // Interpolating across a large jump would render a glide the robot never performed.
    bool snap = (stream.curr.size() != joints.size());
    if (!snap) {
        for (int i = 0; i < joints.size(); ++i) {
            if (std::abs(joints[i] - stream.curr[i]) > kJointSnapDeltaDeg) {
                snap = true;
                break;
            }
        }
    }
    if (snap) {
        stream.prev = joints;
        stream.curr = joints;
        stream.prev_ms = nowMs;
        stream.curr_ms = nowMs;
        stream.shown = joints;
        applyJointsToChain(joints, robot);
        return;
    }
    // Duplicate pose (the backend's 50 Hz UI poll re-publishes between slower wire updates): do
    // NOT advance the stream. Spans must measure the POSE-CHANGE cadence - advancing on duplicates
    // shrank the span to one poll period, and every real change replayed as a 20 ms lurch + hold
    // instead of smooth motion.
    bool changed = false;
    for (int i = 0; i < joints.size(); ++i) {
        if (std::abs(joints[i] - stream.curr[i]) > 1e-9) {
            changed = true;
            break;
        }
    }
    if (!changed) {
        return;
    }
    stream.prev = stream.curr;
    stream.prev_ms = stream.curr_ms;
    stream.curr = joints;
    stream.curr_ms = nowMs;
}

void ViewportPanel::onSmoothTick() {
    if (m_isRebuildingChains) {
        return;
    }
    renderInterpolated(m_mainJointStream, m_mainRobot);
    renderInterpolated(m_ghostJointStream, m_ghostRobot);
}

void ViewportPanel::renderInterpolated(JointStream& stream, RobotChain& robot) {
    if (stream.curr.isEmpty() || stream.prev.size() != stream.curr.size()) {
        return;   // no data yet; the first status packet snaps via pushJointPacket
    }
    // Replay the prev->curr packet segment linearly over the interval FOLLOWING curr, assuming a
    // roughly regular packet cadence: constant velocity, exact arrival at curr, then hold until
    // the next packet. u past 1 simply holds (also covers a paused/stopped stream).
    const qint64 nowMs = m_smoothClock.elapsed();
    const qint64 spanMs = stream.curr_ms - stream.prev_ms;
    double u = 1.0;
    if (spanMs > 0) {
        u = std::clamp(static_cast<double>(nowMs - stream.curr_ms) / static_cast<double>(spanMs),
                       0.0, 1.0);
    }
    QVector<double> pose(stream.curr.size());
    double maxDeltaDeg = (stream.shown.size() == stream.curr.size()) ? 0.0 : 1.0;
    for (int i = 0; i < stream.curr.size(); ++i) {
        pose[i] = stream.prev[i] + (stream.curr[i] - stream.prev[i]) * u;
        if (i < stream.shown.size()) {
            maxDeltaDeg = std::max(maxDeltaDeg, std::abs(pose[i] - stream.shown[i]));
        }
    }
    if (maxDeltaDeg < kJointSettleEpsilonDeg) {
        return;   // settled: a still robot must not churn the scene graph every tick
    }
    stream.shown = pose;
    applyJointsToChain(pose, robot);
}

QVector3D ViewportPanel::flangeWorldPosition() const {
    if (!m_worldRoot) {
        return QVector3D();
    }
    // The flange node if the URDF defines one, else the last actuated axis (the wrist).
    QQuick3DNode* tip = m_mainRobot.flange_entity;
    if (!tip && !m_mainRobot.transforms.isEmpty()) {
        tip = m_mainRobot.transforms.last();
    }
    if (!tip) {
        return QVector3D();
    }
    // Mapped into the Z-up world frame (m_worldRoot) - the frame trajectory data is displayed in -
    // so the value is consistent regardless of the URDF frame correction / mount transform.
    return m_worldRoot->mapPositionFromScene(tip->scenePosition());
}

void ViewportPanel::updateTrajectoryPath(const HmiTrajectoryData& data) {
    m_rawTrajectory = data;
    if (m_trajectory) {
        m_trajectory->setData(data, m_isApproachVisible, /*animateDrawIn=*/true);
        m_trajectory->setVisible(m_isTrajVisible);
    }
}

void ViewportPanel::setProgramExecutionState(bool isRunning, bool isPaused) {
    // A FRESH run starts with a fully bright preview; the fade is intentionally kept after the run
    // ends (finish/stop) as a what-was-executed record. A RESUME from pause must NOT restore
    // brightness (boss review: resume was wiping the executed trace) - fresh start means running
    // rises from a state that was not paused.
    if (isRunning && !m_programRunning && !m_wasPaused && m_trajectory) {
        m_trajectory->resetExecutionProgress();
    }
    m_programRunning = isRunning;
    m_wasPaused = isPaused;
    if (m_trajectory) {
        // The comet marks the execution point: it must stay ON that point through a pause (boss
        // review) and disappear only when the program is genuinely stopped or finished.
        m_trajectory->setExecutionActive(isRunning || isPaused);
    }
}

void ViewportPanel::updateVisualState() {
    if (m_mainRobot.root_entity) {
        m_mainRobot.root_entity->setVisible(m_mainRobot.root_entity->property("chainValid").toBool());
    }
    if (m_ghostRobot.root_entity) {
        const bool ghostValid = m_ghostRobot.root_entity->property("chainValid").toBool();
        m_ghostRobot.root_entity->setVisible(m_isGhostVisible && ghostValid && m_hasGhostDelta);
    }
    if (m_trajectory) {
        m_trajectory->setVisible(m_isTrajVisible);
    }
    if (m_mainRobot.tcp_marker) {
        m_mainRobot.tcp_marker->setVisible(m_isTcpFrameVisible);
    }
}

void ViewportPanel::setTcpFrameVisible(bool visible) {
    m_isTcpFrameVisible = visible;
    updateVisualState();
}

void ViewportPanel::setGhostVisible(bool visible) {
    m_isGhostVisible = visible;
    updateVisualState();
}

void ViewportPanel::setTrajectoryVisible(bool visible) {
    m_isTrajVisible = visible;
    updateVisualState();
}

void ViewportPanel::setApproachVisible(bool visible) {
    m_isApproachVisible = visible;
    if (m_trajectory) {
        // Toggle = instant rebuild, no draw-in sweep: the operator is switching a layer on/off, not
        // receiving a new plan (boss review - the replayed sweep read as a re-plan).
        m_trajectory->setData(m_rawTrajectory, m_isApproachVisible, /*animateDrawIn=*/false);
    }
}

void ViewportPanel::setViewTop() { if (m_quickView && m_quickView->rootObject()) QMetaObject::invokeMethod(m_quickView->rootObject(), "setViewTop"); }
void ViewportPanel::setViewFront() { if (m_quickView && m_quickView->rootObject()) QMetaObject::invokeMethod(m_quickView->rootObject(), "setViewFront"); }
void ViewportPanel::setViewIso() { if (m_quickView && m_quickView->rootObject()) QMetaObject::invokeMethod(m_quickView->rootObject(), "setViewIso"); }
void ViewportPanel::setViewSide() { if (m_quickView && m_quickView->rootObject()) QMetaObject::invokeMethod(m_quickView->rootObject(), "setViewSide"); }
void ViewportPanel::setViewFitToScreen() { if (m_quickView && m_quickView->rootObject()) QMetaObject::invokeMethod(m_quickView->rootObject(), "setViewFitToScreen"); }

} // namespace hexa
