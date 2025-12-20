/**
 * @file PanelView3D.cpp
 * @brief Implementation of the 3D Scene logic.
 *
 * This file contains complex logic for manual Scene Graph construction
 * and raw vertex buffer manipulation for high-performance trajectory rendering.
 */

#include "PanelView3D.h"
#include "../../styles/HexaTheme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <Qt3DExtras/Qt3DWindow>
#include <Qt3DExtras/QForwardRenderer>
#include <Qt3DExtras/QOrbitCameraController>
#include <Qt3DExtras/QPhongAlphaMaterial>
#include <Qt3DExtras/QPhongMaterial>
#include <Qt3DExtras/QCuboidMesh>
#include <Qt3DExtras/QSphereMesh>
#include <Qt3DRender/QMesh>
#include <Qt3DRender/QCamera>
#include <Qt3DRender/QDirectionalLight>
#include <Qt3DCore/QEntity>
#include <Qt3DCore/QTransform>
#include <Qt3DCore/QGeometry>
#include <Qt3DCore/QAttribute>
#include <Qt3DCore/QBuffer>
#include <Qt3DRender/QGeometryRenderer>
#include <Qt3DRender/QPointSize>
#include <Qt3DRender/QShaderProgram>
#include <Qt3DRender/QFilterKey>
#include <Qt3DRender/QParameter>
#include <Qt3DRender/QEffect>
#include <Qt3DRender/QTechnique>
#include <Qt3DRender/QRenderPass>
#include <Qt3DRender/QGraphicsApiFilter>
#include <Qt3DRender/QBlendEquation>
#include <Qt3DRender/QBlendEquationArguments>
#include <QVector3D>
#include <QQuaternion>
#include <QtMath>
#include <QFile>
#include <QDebug>

namespace RDT {

// Physical offset of the TCP relative to the last link (flange)
static const float TCP_OFFSET_Z = -94.3f;
// Global offset to center the robot on the floor
static const QVector3D ROBOT_BASE_OFFSET(0.0f, 0.0f, 0.0f);

// Helper to convert Euler Angles (URDF style) to Quaternion
static QQuaternion fromURDF(float roll_deg, float pitch_deg, float yaw_deg) {
    QQuaternion qRoll  = QQuaternion::fromAxisAndAngle(1.0f, 0.0f, 0.0f, roll_deg);
    QQuaternion qPitch = QQuaternion::fromAxisAndAngle(0.0f, 1.0f, 0.0f, pitch_deg);
    QQuaternion qYaw   = QQuaternion::fromAxisAndAngle(0.0f, 0.0f, 1.0f, yaw_deg);
    // Order matters: Yaw * Pitch * Roll
    return qYaw * qPitch * qRoll;
}

PanelView3D::PanelView3D(QWidget *parent) : QWidget(parent)
{
    // Force OpenGL backend for Qt3D (compatibility)
    qputenv("QT3D_RENDERER", "opengl");
    qputenv("QSG_RHI_BACKEND", "opengl");

    setupUi();
    setup3DScene();
}

PanelView3D::~PanelView3D()
{
}

/**
 * @brief Updates the Vertex Buffers for the trajectory visualization.
 * @details Instead of creating thousands of Entity objects for points,
 * we use a single QGeometry with a raw binary buffer. This is significantly
 * more performant.
 */
void PanelView3D::updateTrajectoryPath(const HmiTrajectoryData &data)
{
    // --- 1. Update Dense Path (LineStrip) ---
    // This draws the continuous line representing the movement path.
    if (m_trajGeometry && m_trajRenderer) {
        if (data.path.isEmpty()) {
            // Hide if empty
            if (!m_trajGeometry->attributes().isEmpty()) {
                Qt3DCore::QAttribute *posAttr = m_trajGeometry->attributes().first();
                posAttr->setCount(0);
                m_trajRenderer->setVertexCount(0);
            }
        } else {
            // Pack QVector3D into raw float bytes: [x, y, z, x, y, z, ...]
            QByteArray bufferBytes;
            bufferBytes.resize(data.path.size() * 3 * sizeof(float));
            float *raw = reinterpret_cast<float*>(bufferBytes.data());

            for (const QVector3D &p : data.path) {
                *raw++ = p.x(); *raw++ = p.y(); *raw++ = p.z();
            }

            // Update the GPU buffer
            if (!m_trajGeometry->attributes().isEmpty()) {
                Qt3DCore::QAttribute *posAttr = m_trajGeometry->attributes().first();
                Qt3DCore::QBuffer *buf = posAttr->buffer();
                buf->setData(bufferBytes);
                posAttr->setCount(data.path.size());
                m_trajRenderer->setVertexCount(data.path.size());
            }
        }
    }

    // --- 2. Update Waypoints (GL_POINTS) ---
    // This draws dots at specific command targets (MOVL, MOVJ).
    // Uses the same buffer packing technique.
    if (m_waypointGeometry && m_waypointRenderer) {
        if (data.waypoints.isEmpty()) {
            if (!m_waypointGeometry->attributes().isEmpty()) {
                Qt3DCore::QAttribute *posAttr = m_waypointGeometry->attributes().first();
                posAttr->setCount(0);
                m_waypointRenderer->setVertexCount(0);
            }
        } else {
            QByteArray bufferBytes;
            bufferBytes.resize(data.waypoints.size() * 3 * sizeof(float));
            float *raw = reinterpret_cast<float*>(bufferBytes.data());

            for (const QVector3D &p : data.waypoints) {
                *raw++ = p.x(); *raw++ = p.y(); *raw++ = p.z();
            }

            if (!m_waypointGeometry->attributes().isEmpty()) {
                Qt3DCore::QAttribute *posAttr = m_waypointGeometry->attributes().first();
                Qt3DCore::QBuffer *buf = posAttr->buffer();
                buf->setData(bufferBytes);
                posAttr->setCount(data.waypoints.size());
                m_waypointRenderer->setVertexCount(data.waypoints.size());
            }
        }
    }
}

/**
 * @brief Animates the robot models.
 * @details Applies rotations to the QTransform components of each joint entity.
 * It handles two robots:
 * 1. Main Robot: Driven by 'actualJoints' (Encoder feedback).
 * 2. Ghost Robot: Driven by 'plannedJoints' (Trajectory planner).
 */
void PanelView3D::updateState(const HmiMotionStatus &status)
{
    // Mode Switching Logic (Sim vs Real colors)
    if (m_isRealMode == status.isSimulated) {
        m_isRealMode = !status.isSimulated;
        updateVisualState();
    }

    // Update Main Robot (Planned/Simulated)
    const QVector<double>& planned = status.plannedJoints;
    if (planned.size() >= 6 && m_mainRobot.transforms.size() >= 6) {
        for (int i = 0; i < 6; ++i) {
            if (m_mainRobot.transforms[i]) {
                // Combine Rest Rotation (Kinematic offset) + Dynamic Rotation (Joint Angle)
                // Rotating around Z-axis is standard for DH parameters.
                QQuaternion dynamicRot = QQuaternion::fromAxisAndAngle(0.0f, 0.0f, 1.0f, planned[i]);
                if (i < m_jointRestRotations.size()) {
                    m_mainRobot.transforms[i]->setRotation(m_jointRestRotations[i] * dynamicRot);
                }
            }
        }
    }

    // Update Ghost Robot (Actual/Feedback) - Only if visible
    if (m_isGhostVisible) {
        const QVector<double>& actual = status.actualJoints;
        if (actual.size() >= 6 && m_ghostRobot.transforms.size() >= 6) {
            for (int i = 0; i < 6; ++i) {
                if (m_ghostRobot.transforms[i]) {
                    QQuaternion dynamicRot = QQuaternion::fromAxisAndAngle(0.0f, 0.0f, 1.0f, actual[i]);
                    if (i < m_jointRestRotations.size()) {
                        m_ghostRobot.transforms[i]->setRotation(m_jointRestRotations[i] * dynamicRot);
                    }
                }
            }
        }
        m_ghostRobot.rootEntity->setEnabled(true);
    } else {
        m_ghostRobot.rootEntity->setEnabled(false);
    }
}

void PanelView3D::setupUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Qt3DWindow is a QWindow, so we need a container widget to embed it
    m_view = new Qt3DExtras::Qt3DWindow();
    m_view->defaultFrameGraph()->setClearColor(QColor(Hexa::Colors::Background));

    m_container = QWidget::createWindowContainer(m_view);
    m_container->setMinimumSize(400, 300);
    m_container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(m_container, 1);
}

void PanelView3D::setup3DScene()
{
    m_rootEntity = new Qt3DCore::QEntity();

    // Camera Setup
    m_camera = m_view->camera();
    if (m_camera) {
        m_camera->lens()->setPerspectiveProjection(45.0f, 16.0f/9.0f, 10.0f, 20000.0f);
        setViewIso();
    }

    // Camera Controls (Orbit)
    m_camController = new Qt3DExtras::QOrbitCameraController(m_rootEntity);
    m_camController->setCamera(m_camera);
    m_camController->setLinearSpeed(2000.0f);
    m_camController->setLookSpeed(180.0f);
    if(m_camera) connect(m_camera, &Qt3DRender::QCamera::positionChanged, this, &PanelView3D::enforceCameraLimits);

    // Lighting (Simple Directional Light)
    Qt3DCore::QEntity *lightEntity = new Qt3DCore::QEntity(m_rootEntity);
    Qt3DRender::QDirectionalLight *light = new Qt3DRender::QDirectionalLight(lightEntity);
    light->setWorldDirection(QVector3D(-1.0f, -1.0f, -1.0f));
    light->setColor(Qt::white); light->setIntensity(1.5f);
    lightEntity->addComponent(light);

    // Floor (Reference Plane)
    Qt3DCore::QEntity *floorEntity = new Qt3DCore::QEntity(m_rootEntity);
    Qt3DExtras::QCuboidMesh *floorMesh = new Qt3DExtras::QCuboidMesh();
    floorMesh->setXExtent(3000.0f); floorMesh->setYExtent(3000.0f); floorMesh->setZExtent(20.0f);
    Qt3DExtras::QPhongMaterial *floorMat = new Qt3DExtras::QPhongMaterial();
    floorMat->setAmbient(QColor(Hexa::Colors::Surface));
    floorMat->setDiffuse(QColor(Hexa::Colors::SurfaceLight));
    floorMat->setSpecular(QColor(Hexa::Colors::Accent));
    floorMat->setShininess(10.0f);
    Qt3DCore::QTransform *floorTrans = new Qt3DCore::QTransform();
    floorTrans->setTranslation(QVector3D(0.0f, 0.0f, -10.0f));
    floorEntity->addComponent(floorMesh); floorEntity->addComponent(floorMat); floorEntity->addComponent(floorTrans);

    // Robot Construction (Manual Kinematic Chain)
    m_jointRestRotations.clear();
    m_mainRobot = buildRobotChain(m_rootEntity, "MainRobot", true);
    m_ghostRobot = buildRobotChain(m_rootEntity, "GhostRobot", false);

    // --- SETUP TRAJECTORY RENDERING (Custom Shader) ---
    // We use a custom shader to render points as circles instead of squares.

    m_trajEntity = new Qt3DCore::QEntity(m_rootEntity);
    m_trajGeometry = new Qt3DCore::QGeometry(m_trajEntity);

    // Create buffer (Empty initially)
    Qt3DCore::QBuffer *trajBuf = new Qt3DCore::QBuffer(m_trajGeometry);
    trajBuf->setData(QByteArray(3 * sizeof(float), 0));

    // Define Position Attribute
    Qt3DCore::QAttribute *posAttr = new Qt3DCore::QAttribute();
    posAttr->setName(Qt3DCore::QAttribute::defaultPositionAttributeName());
    posAttr->setVertexBaseType(Qt3DCore::QAttribute::Float);
    posAttr->setVertexSize(3);
    posAttr->setAttributeType(Qt3DCore::QAttribute::VertexAttribute);
    posAttr->setBuffer(trajBuf);
    posAttr->setByteStride(3 * sizeof(float));
    posAttr->setCount(0);
    m_trajGeometry->addAttribute(posAttr);

    m_trajRenderer = new Qt3DRender::QGeometryRenderer();
    m_trajRenderer->setGeometry(m_trajGeometry);
    m_trajRenderer->setPrimitiveType(Qt3DRender::QGeometryRenderer::LineStrip);
    m_trajRenderer->setVertexCount(0);

    // Waypoints setup (Similar logic, Points primitive)
    m_waypointEntity = new Qt3DCore::QEntity(m_rootEntity);
    m_waypointGeometry = new Qt3DCore::QGeometry(m_waypointEntity);
    Qt3DCore::QBuffer *wpBuf = new Qt3DCore::QBuffer(m_waypointGeometry);
    wpBuf->setData(QByteArray(3 * sizeof(float), 0));
    Qt3DCore::QAttribute *wpPosAttr = new Qt3DCore::QAttribute();
    wpPosAttr->setName(Qt3DCore::QAttribute::defaultPositionAttributeName());
    wpPosAttr->setVertexBaseType(Qt3DCore::QAttribute::Float);
    wpPosAttr->setVertexSize(3);
    wpPosAttr->setAttributeType(Qt3DCore::QAttribute::VertexAttribute);
    wpPosAttr->setBuffer(wpBuf);
    wpPosAttr->setByteStride(3 * sizeof(float));
    wpPosAttr->setCount(0);
    m_waypointGeometry->addAttribute(wpPosAttr);
    m_waypointRenderer = new Qt3DRender::QGeometryRenderer();
    m_waypointRenderer->setGeometry(m_waypointGeometry);
    m_waypointRenderer->setPrimitiveType(Qt3DRender::QGeometryRenderer::Points);
    m_waypointRenderer->setVertexCount(0);

    // --- Custom Shader for Points/Lines ---
    Qt3DRender::QShaderProgram *shader = new Qt3DRender::QShaderProgram();
    shader->setVertexShaderCode(R"(
        #version 150 core
        in vec3 vertexPosition;
        uniform mat4 mvp;
        uniform float pointSize;
        void main() {
            gl_Position = mvp * vec4(vertexPosition, 1.0);
            gl_PointSize = pointSize;
        }
    )");
    shader->setFragmentShaderCode(R"(
        #version 150 core
        out vec4 fragColor;
        uniform vec3 pointColor;
        uniform float pointSize;
        void main() {
            // Discard fragments outside circle radius (make points round)
            // gl_PointCoord is available in Fragment Shader for Points
            if (pointSize > 5.0) {
                 vec2 coord = gl_PointCoord - vec2(0.5);
                 if (length(coord) > 0.5) discard;
            }
            fragColor = vec4(pointColor, 1.0);
        }
    )");

    // Helper to create material with custom shader parameters
    auto createMaterial = [&](QColor color, float size) {
        Qt3DRender::QMaterial *mat = new Qt3DRender::QMaterial();
        Qt3DRender::QEffect *eff = new Qt3DRender::QEffect();
        Qt3DRender::QTechnique *tech = new Qt3DRender::QTechnique();
        Qt3DRender::QRenderPass *pass = new Qt3DRender::QRenderPass();

        pass->setShaderProgram(shader);

        Qt3DRender::QPointSize *ps = new Qt3DRender::QPointSize();
        ps->setSizeMode(Qt3DRender::QPointSize::Fixed);
        ps->setValue(size);
        pass->addRenderState(ps);

        tech->addRenderPass(pass);
        Qt3DRender::QFilterKey *fk = new Qt3DRender::QFilterKey();
        fk->setName(QStringLiteral("renderingStyle"));
        fk->setValue(QStringLiteral("forward"));
        tech->addFilterKey(fk);

        // Graphics API Filter
        Qt3DRender::QGraphicsApiFilter *api = tech->graphicsApiFilter();
        api->setApi(Qt3DRender::QGraphicsApiFilter::OpenGL);
        api->setProfile(Qt3DRender::QGraphicsApiFilter::CoreProfile);
        api->setMajorVersion(3);
        api->setMinorVersion(2);

        eff->addTechnique(tech);
        eff->addParameter(new Qt3DRender::QParameter(QStringLiteral("pointColor"), color));
        eff->addParameter(new Qt3DRender::QParameter(QStringLiteral("pointSize"), size));
        mat->setEffect(eff);
        return mat;
    };

    // Attach Components
    m_trajEntity->addComponent(m_trajRenderer);
    m_trajEntity->addComponent(createMaterial(QColor("#FF5722"), 2.0f)); // Lines

    m_waypointEntity->addComponent(m_waypointRenderer);
    m_waypointEntity->addComponent(createMaterial(QColor("#00E676"), 12.0f)); // Waypoints

    m_isRealMode = false;
    m_isGhostVisible = true;
    updateVisualState();

    m_view->setRootEntity(m_rootEntity);
}

/**
 * @brief Manually constructs the Scene Graph for the robot.
 * @details This mimics URDF parsing but is hardcoded for the specific KUKA LBR iisy 11 model.
 * Each link is an Entity child of the previous link's Entity.
 *
 * Hierarchy: Root -> Base -> Link1 -> Link2 -> ... -> Link6 -> TCP
 */
PanelView3D::RobotChain PanelView3D::buildRobotChain(Qt3DCore::QEntity *rootParent, const QString &nameSuffix, bool showTcp)
{
    RobotChain chain;
    chain.rootEntity = new Qt3DCore::QEntity(rootParent);
    chain.rootEntity->setObjectName("RobotRoot_" + nameSuffix);

    Qt3DCore::QTransform* rootTrans = new Qt3DCore::QTransform();
    rootTrans->setTranslation(ROBOT_BASE_OFFSET);
    chain.rootEntity->addComponent(rootTrans);

    // Base Link
    Qt3DCore::QEntity *baseLinkEntity = new Qt3DCore::QEntity(chain.rootEntity);
    Qt3DCore::QEntity *baseVisualEntity = new Qt3DCore::QEntity(baseLinkEntity);
    Qt3DRender::QMesh *baseMesh = new Qt3DRender::QMesh();
    baseMesh->setSource(QUrl("qrc:/meshes/lbr_iisy11_r1300/visual/base_link.stl"));
    Qt3DExtras::QPhongAlphaMaterial *baseMat = new Qt3DExtras::QPhongAlphaMaterial();
    Qt3DCore::QTransform *baseVisualTrans = new Qt3DCore::QTransform();
    baseVisualTrans->setScale(1000.0f); // STL is in meters, Scene in mm? Or scale fix.
    baseVisualEntity->addComponent(baseMesh); baseVisualEntity->addComponent(baseMat); baseVisualEntity->addComponent(baseVisualTrans);
    chain.materials.append(baseMat);

    // Only populate rest rotations once (shared between Main and Ghost)
    bool populateRest = m_jointRestRotations.isEmpty();

    // Link Creation (Parameters from datasheet/URDF)
    // Args: Parent, Mesh Path, Translation from Parent, Rotation from Parent (Rest Pose)

    // L1: Z=184.5, Rot X=180
    auto L1 = createLink(baseLinkEntity, "qrc:/meshes/lbr_iisy11_r1300/visual/link_1.stl", QVector3D(0, 0, 184.5f), fromURDF(180, 0, 0));

    // L2: Y=101.1, Z=-115.5, Rot X=90
    auto L2 = createLink(L1.jointEntity, "qrc:/meshes/lbr_iisy11_r1300/visual/link_2.stl", QVector3D(0, 101.1f, -115.5f), fromURDF(90, 0, 0));

    // L3: X=590.0, Z=23.7, No Rot
    auto L3 = createLink(L2.jointEntity, "qrc:/meshes/lbr_iisy11_r1300/visual/link_3.stl", QVector3D(590.0f, 0, 23.7f), fromURDF(0, 0, 0));

    // L4: X=113.9, Z=77.4, Rot X=90, Rot Z=-90
    auto L4 = createLink(L3.jointEntity, "qrc:/meshes/lbr_iisy11_r1300/visual/link_4.stl", QVector3D(113.9f, 0, 77.4f), fromURDF(90, 0, -90));

    // L5: Y=50.7, Z=-418.1, Rot Y=90, Rot Z=90
    auto L5 = createLink(L4.jointEntity, "qrc:/meshes/lbr_iisy11_r1300/visual/link_5.stl", QVector3D(0, 50.7f, -418.1f), fromURDF(0, 90, 90));

    // L6: X=83.7, Z=-50.7, Rot X=90, Rot Z=-90
    auto L6 = createLink(L5.jointEntity, "qrc:/meshes/lbr_iisy11_r1300/visual/link_6.stl", QVector3D(83.7f, 0, -50.7f), fromURDF(90, 0, -90));

    // Optional TCP visualization (Green Sphere)
    if (showTcp) {
        Qt3DCore::QEntity* tcpEntity = new Qt3DCore::QEntity(L6.jointEntity);
        Qt3DCore::QTransform* tcpTrans = new Qt3DCore::QTransform();
        tcpTrans->setTranslation(QVector3D(0.0f, 0.0f, TCP_OFFSET_Z));
        Qt3DExtras::QSphereMesh* tcpMesh = new Qt3DExtras::QSphereMesh();
        tcpMesh->setRadius(10.0f);
        Qt3DExtras::QPhongMaterial* tcpMat = new Qt3DExtras::QPhongMaterial();
        tcpMat->setDiffuse(QColor(Qt::green));
        tcpMat->setAmbient(QColor(Qt::green));
        tcpEntity->addComponent(tcpTrans);
        tcpEntity->addComponent(tcpMesh);
        tcpEntity->addComponent(tcpMat);
    }

    // Store references for updates
    chain.transforms = {L1.transform, L2.transform, L3.transform, L4.transform, L5.transform, L6.transform};
    chain.materials.append({L1.material, L2.material, L3.material, L4.material, L5.material, L6.material});

    if (populateRest) {
        m_jointRestRotations.append(fromURDF(180, 0, 0)); m_jointRestRotations.append(fromURDF(90, 0, 0));
        m_jointRestRotations.append(fromURDF(0, 0, 0)); m_jointRestRotations.append(fromURDF(90, 0, -90));
        m_jointRestRotations.append(fromURDF(0, 90, 90)); m_jointRestRotations.append(fromURDF(90, 0, -90));
    }
    return chain;
}

PanelView3D::LinkComponents PanelView3D::createLink(Qt3DCore::QEntity *parentEntity, const QString &meshPath, const QVector3D &originPos, const QQuaternion &originRot)
{
    // The Joint Entity holds the Transform (Kinematics)
    Qt3DCore::QEntity *jointEntity = new Qt3DCore::QEntity(parentEntity);
    Qt3DCore::QTransform *jointTrans = new Qt3DCore::QTransform();
    jointTrans->setTranslation(originPos);
    jointTrans->setRotation(originRot);
    jointEntity->addComponent(jointTrans);

    // The Visual Entity holds the Mesh (Graphics)
    // Separation allows scaling the mesh without affecting the kinematic chain
    Qt3DCore::QEntity *visualEntity = new Qt3DCore::QEntity(jointEntity);
    Qt3DRender::QMesh *mesh = new Qt3DRender::QMesh();
    mesh->setSource(QUrl(meshPath));

    Qt3DExtras::QPhongAlphaMaterial *mat = new Qt3DExtras::QPhongAlphaMaterial();

    Qt3DCore::QTransform *visualTrans = new Qt3DCore::QTransform();
    visualTrans->setScale(1000.0f); // Convert units if necessary

    visualEntity->addComponent(mesh);
    visualEntity->addComponent(mat);
    visualEntity->addComponent(visualTrans);

    return {jointEntity, jointTrans, mat};
}

void PanelView3D::updateVisualState()
{
    // Colors for different states
    QColor colActive("#FF5722");
    QColor colActiveBase(Hexa::Colors::SurfaceLight);
    QColor colGhostSim("#66FCF1"); // Cyan for Ghost

    // Main Robot Material Update
    m_mainRobot.rootEntity->setEnabled(true);
    for (int i=0; i<m_mainRobot.materials.size(); ++i) {
        auto *mat = m_mainRobot.materials[i];
        if (i == 0) mat->setDiffuse(colActiveBase); // Base is darker
        else mat->setDiffuse(colActive);
        mat->setSpecular(Qt::white);
        mat->setShininess(50.0f);
        mat->setAlpha(1.0f);
    }

    // Ghost Robot Material Update (Semi-transparent)
    for (auto *mat : m_ghostRobot.materials) {
        mat->setDiffuse(colGhostSim);
        mat->setAmbient(colGhostSim);
        mat->setSpecular(Qt::transparent);
        mat->setAlpha(0.3f);
        mat->setShininess(0.0f);
    }
    m_ghostRobot.rootEntity->setEnabled(m_isGhostVisible);

    if (m_trajEntity) m_trajEntity->setEnabled(m_isTrajVisible);
    if (m_waypointEntity) m_waypointEntity->setEnabled(m_isTrajVisible);
}

void PanelView3D::setGhostVisible(bool visible) { m_isGhostVisible = visible; updateVisualState(); }
void PanelView3D::setTrajectoryVisible(bool visible) { m_isTrajVisible = visible; updateVisualState(); }

// Simple Camera Presets
void PanelView3D::setViewTop() { if (!m_camera) return; m_camera->setViewCenter(QVector3D(0.0f, 0.0f, 600.0f)); m_camera->setUpVector(QVector3D(0.0f, 1.0f, 0.0f)); m_camera->setPosition(QVector3D(0.1f, 0.0f, 3000.0f)); enforceCameraLimits(); }
void PanelView3D::setViewFront() { if (!m_camera) return; m_camera->setViewCenter(QVector3D(0.0f, 0.0f, 600.0f)); m_camera->setUpVector(QVector3D(0.0f, 0.0f, 1.0f)); m_camera->setPosition(QVector3D(3000.0f, 0.0f, 600.0f)); }
void PanelView3D::setViewIso() { if (!m_camera) return; m_camera->setViewCenter(QVector3D(0.0f, 0.0f, 600.0f)); m_camera->setUpVector(QVector3D(0.0f, 0.0f, 1.0f)); m_camera->setPosition(QVector3D(2000.0f, 2000.0f, 2000.0f)); }

void PanelView3D::enforceCameraLimits() {
    if (!m_camera) return;
    QVector3D targetCenter(0.0f, 0.0f, 600.0f);

    // Prevent drifting away from center
    if (m_camera->viewCenter() != targetCenter) { m_camera->setViewCenter(targetCenter); }

    QVector3D pos = m_camera->position();
    QVector3D upVec(0.0f, 0.0f, 1.0f);

    // Prevent camera from going underground (Z < 0) unless looking top-down
    // (Simple hack logic for better UX)
    bool lookingDown = (qAbs(pos.x()) < 500.0f && qAbs(pos.y()) < 500.0f && pos.z() > 1000.0f);
    if (!lookingDown && m_camera->upVector() != upVec) { m_camera->setUpVector(upVec); }
}

} // namespace RDT
