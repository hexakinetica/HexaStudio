// ViewportScene.qml — module-owned cinematic 3D scene for the viewport3d module.
//
// Render-level look (boss requirement) while staying an industrial HMI:
//   - Image-Based Lighting from a PROCEDURAL studio sky (no external HDR asset shipped) gives soft
//     ambient + real reflections on the metal robot links.
//   - Filmic tonemapping, SSAO (contact shadows), a subtle bloom + vignette, and dithering to kill
//     banding on the dark background.
//   - A 3-point studio light rig (key with soft cascaded shadows, cool fill, warm rim).
//
// C++ contract preserved 1:1 with the shipping PanelView3D scene so ViewportPanel drops in unchanged:
//   - property alias sceneRoot  (C++ parents the world content under it)
//   - functions setViewTop/Front/Side/Iso/FitToScreen (invoked from C++ via QMetaObject)
//   - orbit / pan / zoom mouse controls.
import QtQuick
import QtQuick3D

Item {
    id: root
    width: 800
    height: 600

    property alias sceneRoot: sceneRootNode
    property alias camera: orbitCamera
    property vector3d orbitCenter: Qt.vector3d(0, 320, 0)
    property real orbitYawDeg: 45
    property real orbitPitchDeg: -22
    property real orbitDistance: 1450
    property real orbitMinDistance: 300
    property real orbitMaxDistance: 14000

    function clamp(v, minV, maxV) {
        return Math.max(minV, Math.min(maxV, v))
    }

    function degToRad(v) {
        return v * Math.PI / 180.0
    }

    // Showroom turntable (investor-look batch): after idleRearmTimer.interval with no camera input
    // the scene orbits slowly, so the robot reads as a product on a stand. ANY camera input - orbit,
    // pan, zoom or a view preset - stops it and re-arms the idle timer. Plain property updates on
    // the existing orbit yaw: no extra GPU features (weak-driver compatibility mandate).
    property real idleOrbitDegPerSec: 2.0

    // Camera automation is FORBIDDEN while the robot moves (boss safety review): camera motion on
    // top of robot motion is disorienting. C++ pushes this from the live motion status; a
    // transition into motion stops an active orbit immediately and re-arms the idle timer.
    property bool motionActive: false
    onMotionActiveChanged: if (motionActive) notifyCameraInteraction()

    Timer {
        id: idleRearmTimer
        interval: 15000
        running: true
        onTriggered: {
            if (root.motionActive) {
                idleRearmTimer.restart()   // robot is working: keep waiting, no auto-orbit
            } else {
                turntableTimer.running = true
            }
        }
    }

    Timer {
        id: turntableTimer
        interval: 16
        repeat: true
        running: false
        onTriggered: root.orbitYawDeg = (root.orbitYawDeg + root.idleOrbitDegPerSec * interval / 1000.0) % 360
    }

    function notifyCameraInteraction() {
        turntableTimer.running = false
        idleRearmTimer.restart()
    }

    function setViewTop() {
        notifyCameraInteraction()
        orbitYawDeg = 0
        orbitPitchDeg = -89
    }

    function setViewFront() {
        notifyCameraInteraction()
        orbitYawDeg = 180
        orbitPitchDeg = -8
    }

    function setViewSide() {
        notifyCameraInteraction()
        orbitYawDeg = 90
        orbitPitchDeg = -8
    }

    function setViewIso() {
        notifyCameraInteraction()
        orbitYawDeg = 45
        orbitPitchDeg = -28
    }

    function setViewFitToScreen() {
        notifyCameraInteraction()
        orbitDistance = 1450
        orbitCenter = Qt.vector3d(0, 320, 0)
    }

    View3D {
        id: view3D
        anchors.fill: parent

        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Color
            clearColor: "#20242e"

            // This AMD driver (Radeon 740M, Qt 6.11) renders to black with QtQuick3D's advanced GPU
            // features, so they are OFF: IBL lightProbe crashes createEnvironmentMap, and the
            // ExtendedSceneEnvironment post-effects (SSAO/glow/vignette) black out the frame. A plain
            // SceneEnvironment with filmic tonemap + MSAA and the 3-point studio light rig renders
            // cleanly and is the driver-safe baseline. Revisit the effects when the driver/Qt issue is
            // resolved (or on a different GPU).
            tonemapMode: SceneEnvironment.TonemapModeFilmic

            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
        }

        // Scene composition. All world-frame content (robot, TCP marker, trajectory, waypoints) is
        // parented by C++ under sceneRootNode (through a Z-up->Y-up world node); the stage below
        // (camera, lights, floor) stays in the QML Y-up scene.
        Node {
            id: sceneRootNode

            Node {
                id: cameraPivot
                position: root.orbitCenter
                eulerRotation: Qt.vector3d(root.orbitPitchDeg, root.orbitYawDeg, 0)

                PerspectiveCamera {
                    id: orbitCamera
                    z: root.orbitDistance
                    clipNear: 10.0
                    clipFar: 20000.0
                    fieldOfView: 42
                }
            }

            // Key light — the shaping light, with soft cascaded shadows. NEUTRAL white: the old warm
            // key (#fff4e6) read as a yellow cast on the white/aluminum HexaArmMedium shells (boss
            // review 2026-07-07); an industrial robot cell wants clean neutral metal, not sunset.
            DirectionalLight {
                eulerRotation.x: -52
                eulerRotation.y: -40
                color: "#ffffff"
                brightness: 2.6
                castsShadow: true
                shadowMapQuality: Light.ShadowMapQualityHigh
                shadowFactor: 70
                shadowBias: 12
                softShadowQuality: Light.PCF16
                pcfFactor: 6
            }

            // Fill light — cool, soft, no shadow (lifts the shadow side without a second shadow).
            DirectionalLight {
                eulerRotation.x: -12
                eulerRotation.y: 55
                color: "#cdd6ff"
                brightness: 1.15
            }

            // Rim / back light — separates the robot silhouette from the dark background.
            // Cool-neutral (was warm #ffe9cf) for the same no-yellow-cast reason as the key.
            DirectionalLight {
                eulerRotation.x: 18
                eulerRotation.y: 160
                color: "#dfe6f2"
                brightness: 1.4
            }

            // Studio floor — large, matte, receives the key shadow; the IBL gives it a soft sheen.
            Model {
                id: floorModel
                source: "#Rectangle"
                eulerRotation.x: -90
                y: -0.5
                scale: Qt.vector3d(80, 80, 1)
                receivesShadows: true
                castsShadows: false
                materials: PrincipledMaterial {
                    baseColor: "#1b1e27"
                    roughness: 0.95
                    metalness: 0.0
                    specularAmount: 0.05
                }
            }

            // Stage rings (investor-look batch): three concentric hairline circles around the robot
            // base - the showroom-podium floor marking. Each ring is a closed run of thin, flat,
            // unlit chord boxes: baseline primitives and materials ONLY, no textures or shaders
            // (weak-driver compatibility mandate). Static geometry, built once at load.
            component StageRing : Node {
                id: ringRoot
                property real radiusMm: 300
                property color ringColor: "#2E3542"
                readonly property int chordCount: 72
                Repeater3D {
                    model: ringRoot.chordCount
                    Model {
                        source: "#Cube"   // built-in cube is 100 units on a side
                        position: Qt.vector3d(
                            ringRoot.radiusMm * Math.cos(index * 2 * Math.PI / ringRoot.chordCount),
                            0.8,   // just above the floor plane: avoids coplanar z-fighting
                            ringRoot.radiusMm * Math.sin(index * 2 * Math.PI / ringRoot.chordCount))
                        eulerRotation.y: -(index * 360 / ringRoot.chordCount) - 90
                        // Chord long enough to overlap its neighbours; hairline wide, paper thin.
                        scale: Qt.vector3d((2 * Math.PI * ringRoot.radiusMm / ringRoot.chordCount + 3) / 100,
                                           0.01, 0.025)
                        castsShadows: false
                        receivesShadows: false
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColor: ringRoot.ringColor
                        }
                    }
                }
            }
            StageRing { radiusMm: 280; ringColor: "#333B4A" }
            StageRing { radiusMm: 430; ringColor: "#2C3340" }
            StageRing { radiusMm: 580; ringColor: "#262C38" }
        }
    }

    MouseArea {
        id: cadMouseController
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        hoverEnabled: true
        preventStealing: true

        property real lastX: 0
        property real lastY: 0
        property bool orbitMode: false
        property bool panMode: false

        onPressed: function(mouse) {
            root.notifyCameraInteraction()
            lastX = mouse.x
            lastY = mouse.y

            // Left mouse button orbits; right pans; middle orbits (Shift+middle pans).
            orbitMode = (mouse.button === Qt.LeftButton) || (mouse.button === Qt.MiddleButton && !(mouse.modifiers & Qt.ShiftModifier))
            panMode = (mouse.button === Qt.RightButton) || (mouse.button === Qt.MiddleButton && (mouse.modifiers & Qt.ShiftModifier))
        }

        onReleased: function(_) {
            orbitMode = false
            panMode = false
            cursorShape = Qt.ArrowCursor
        }

        onPositionChanged: function(mouse) {
            const dx = mouse.x - lastX
            const dy = mouse.y - lastY
            lastX = mouse.x
            lastY = mouse.y

            if (orbitMode) {
                cursorShape = Qt.SizeAllCursor
                orbitYawDeg = orbitYawDeg - dx * 0.28
                // Clamp pitch so the camera stays above the floor.
                orbitPitchDeg = clamp(orbitPitchDeg - dy * 0.22, -89, -1)
                return
            }

            if (panMode) {
                cursorShape = Qt.OpenHandCursor

                const panScale = orbitDistance * 0.0016
                const yaw = degToRad(orbitYawDeg)
                const rightX = Math.cos(yaw)
                const rightZ = -Math.sin(yaw)

                orbitCenter = Qt.vector3d(
                    orbitCenter.x - rightX * dx * panScale,
                    clamp(orbitCenter.y + dy * panScale, 0, 10000),
                    orbitCenter.z - rightZ * dx * panScale
                )
            }
        }

        onWheel: function(wheel) {
            root.notifyCameraInteraction()
            const zoomScale = wheel.angleDelta.y > 0 ? 0.90 : 1.10
            orbitDistance = clamp(orbitDistance * zoomScale, orbitMinDistance, orbitMaxDistance)
            wheel.accepted = true
        }
    }

    // Auto-orbit badge: while the turntable rotates the CAMERA, say so explicitly - on a robot HMI
    // an unlabelled slow scene rotation reads as "the robot started moving" (boss review). 2D
    // QtQuick overlay, no GPU features involved.
    Rectangle {
        visible: turntableTimer.running
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 12
        width: badgeRow.width + 28
        height: 32
        radius: 4
        color: "#CC141922"
        border.color: "#3A4250"
        border.width: 1
        Row {
            id: badgeRow
            anchors.centerIn: parent
            spacing: 8
            Rectangle {
                width: 8
                height: 8
                radius: 4
                color: "#E9EDF2"
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: "CAMERA AUTO-ORBIT"
                color: "#E9EDF2"
                font.pixelSize: 13
                font.letterSpacing: 1
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    Component.onCompleted: {
        setViewFitToScreen()
        setViewIso()
    }
}
