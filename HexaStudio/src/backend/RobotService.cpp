#include "RobotService.h"
#include <QDebug>
#include <QDateTime>
#include <algorithm>
#include <QVector3D>

RobotService* RobotService::m_instance = nullptr;

RobotService* RobotService::instance() {
    if (!m_instance) {
        m_instance = new RobotService();
    }
    return m_instance;
}

RobotService::RobotService(QObject *parent) : QObject(parent)
{
    // Init status
    m_status.motion.actualJoints.fill(0.0, 6);
    m_status.motion.plannedJoints.fill(0.0, 6);
    m_status.top.isConnected = false;

    // Init Config with Defaults
    m_config.network.controllerIp = "127.0.0.1";
    m_config.network.controllerPort = 30002;
    m_config.network.hmiIp = "127.0.0.1";

    // Reset State
    m_controlState = {};
    m_controlState.speedOverride = 0.5;
    m_controlState.jogRequestId = 0;
    m_controlState.configUpdateReqId = 0;
    m_controlState.programUpdateReqId = 0;

    // Start global counter > 0
    m_globalRequestId = 10;

    // Init versions to 0 so we sync immediately on connect
    m_controlState.ackConfigVersion = 0;
    m_controlState.ackTrajVersion = 0;
    m_controlState.ackProgramVersion = 0;

    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected, this, &RobotService::onSocketConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &RobotService::onSocketDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &RobotService::onSocketReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &RobotService::onSocketError);

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setInterval(2000);
    connect(m_reconnectTimer, &QTimer::timeout, this, [this](){
        if(m_socket->state() == QAbstractSocket::UnconnectedState) {
            m_socket->connectToHost(m_config.network.controllerIp, m_config.network.controllerPort);
        }
    });
    m_reconnectTimer->start();

    m_flushTimer = new QTimer(this);
    connect(m_flushTimer, &QTimer::timeout, this, &RobotService::onStateFlushTimer);
    m_flushTimer->start(20);

    qDebug() << "[R-SERVICE] Started. Target: " << m_config.network.controllerIp << ":" << m_config.network.controllerPort;
}

void RobotService::onSocketConnected() {
    qDebug() << "[R-SERVICE] Connected.";
    m_status.top.isConnected = true;

    // Reset versions to force sync (Pull full state)
    m_controlState.ackConfigVersion = 0;
    m_controlState.ackTrajVersion = 0;
    m_controlState.ackProgramVersion = 0;

    emit stateChanged(m_status);
}

void RobotService::onSocketDisconnected() {
    qDebug() << "[R-SERVICE] Disconnected.";
    m_status.top.isConnected = false;
    emit stateChanged(m_status);
}

void RobotService::onSocketError(QAbstractSocket::SocketError) {}

void RobotService::onSocketReadyRead() {
    m_receiveBuffer.append(m_socket->readAll());
    while (m_receiveBuffer.contains('\n')) {
        int pos = m_receiveBuffer.indexOf('\n');
        QByteArray line = m_receiveBuffer.left(pos);
        m_receiveBuffer.remove(0, pos + 1);

        QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isNull() && doc.isObject()) {
            processJsonPacket(doc.object());
        }
    }
}

void RobotService::onStateFlushTimer() {
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        flushControlState();
    }
}

void RobotService::flushControlState() {
    QJsonObject payload;

    payload["jogRequestId"] = (int)m_controlState.jogRequestId;
    payload["jogAxis"] = m_controlState.jogAxis;
    payload["jogIncrement"] = m_controlState.jogIncrement;

    payload["speedOverride"] = m_controlState.speedOverride;
    payload["enableRealMode"] = m_controlState.enableRealMode;
    payload["monitorToolId"] = m_controlState.monitorToolId;
    payload["monitorBaseId"] = m_controlState.monitorBaseId;

    payload["setEStop"] = m_controlState.setEStop;
    payload["resetEStop"] = m_controlState.resetEStop;
    m_controlState.setEStop = false;
    m_controlState.resetEStop = false;

    payload["programCommand"] = m_controlState.programCommand;
    m_controlState.programCommand = 0;

    payload["previewStepIndex"] = m_controlState.previewStepIndex;

    payload["ackConfigVersion"] = (int)m_controlState.ackConfigVersion;
    payload["ackTrajVersion"] = (int)m_controlState.ackTrajVersion;
    payload["ackProgramVersion"] = (int)m_controlState.ackProgramVersion;

    payload["configUpdateReqId"] = (int)m_controlState.configUpdateReqId;
    payload["programUpdateReqId"] = (int)m_controlState.programUpdateReqId;

    // Config Upload
    if (m_controlState.configUpdateReqId > 0) {
        QJsonObject cfgObj;
        cfgObj["ipAddress"] = QString::fromStdString(m_controlState.newConfig.ipAddress);
        QJsonArray toolsArr;
        for (const auto &t : m_controlState.newConfig.tools) {
            QJsonObject o; o["id"] = t.id; o["name"] = QString::fromStdString(t.name);
            QJsonArray a; for(double d:t.offset)a.append(d); o["offset"]=a;
            toolsArr.append(o);
        }
        cfgObj["tools"] = toolsArr;
        QJsonArray basesArr;
        for (const auto &b : m_controlState.newConfig.bases) {
            QJsonObject o; o["id"] = b.id; o["name"] = QString::fromStdString(b.name);
            QJsonArray a; for(double d:b.offset)a.append(d); o["offset"]=a;
            basesArr.append(o);
        }
        cfgObj["bases"] = basesArr;
        QJsonArray limitsArr;
        for(int i=0; i<6; ++i) { QJsonArray l; l.append(-180.0); l.append(180.0); limitsArr.append(l); }
        cfgObj["axisLimits"] = limitsArr;
        payload["newConfig"] = cfgObj;
    }

    // Program Upload
    if (m_controlState.programUpdateReqId > 0) {
        QJsonObject progObj;
        progObj["name"] = QString::fromStdString(m_controlState.newProgram.name);
        QJsonArray steps;
        for(const auto& s : m_controlState.newProgram.steps) {
            QJsonObject st;
            st["id"] = s.id; st["type"] = s.type; st["speed"] = s.speed;
            st["commandCode"] = QString::fromStdString(s.commandCode);
            QJsonArray jts; for(double v : s.targetJoints) jts.append(v);
            st["targetJoints"] = jts;
            steps.append(st);
        }
        progObj["steps"] = steps;
        payload["newProgram"] = progObj;
    }

    QJsonObject root;
    root["type"] = static_cast<int>(Rdt::PacketType::CONTROL_STATE);
    root["payload"] = payload;

    m_socket->write(QJsonDocument(root).toJson(QJsonDocument::Compact) + "\n");
}

void RobotService::processJsonPacket(const QJsonObject &root) {
    int type = root["type"].toInt();
    if (type == static_cast<int>(Rdt::PacketType::STATUS_UPDATE)) {
        handleStatusUpdate(root["payload"].toObject());
    }
}

void RobotService::handleStatusUpdate(const QJsonObject &pl) {
    QJsonObject mot = pl["motion"].toObject();

    auto parseVec = [](const QJsonArray &arr, QVector<double> &out) {
        for(int i=0; i<6 && i<arr.size(); ++i) out[i] = arr[i].toDouble();
    };
    parseVec(mot["actualJoints"].toArray(), m_status.motion.actualJoints);
    parseVec(mot["plannedJoints"].toArray(), m_status.motion.plannedJoints);
    parseVec(mot["monitorPose"].toArray(), m_status.motion.monitorPose);

    m_status.motion.displayJoints = m_status.motion.plannedJoints;
    m_status.motion.displayTcp = m_status.motion.monitorPose;

    m_status.motion.isMoving = mot["isMoving"].toBool();
    m_status.motion.isSimulated = mot["isSimulated"].toBool();

    if (mot.contains("speedRatio")) {
        m_status.motion.speedOverride = mot["speedRatio"].toDouble();
    }

    QJsonObject sys = pl["sys"].toObject();
    m_status.top.isEStop = sys["isEStop"].toBool();
    m_status.top.mode = m_status.motion.isSimulated ? "SIM" : "REAL";
    m_status.top.cpuLoad = sys["cpuLoad"].toDouble();
    m_status.top.controllerTemp = sys["controllerTemp"].toDouble();
    m_status.top.networkLatency = sys["networkLatency"].toDouble();

    QJsonObject prog = pl["prog"].toObject();
    m_status.prog.isRunning = prog["isRunning"].toBool();
    m_status.prog.currentRowIndex = prog["currentLine"].toInt();
    m_status.prog.loadedProgramName = prog["programName"].toString();

    // Transaction Acks - Stop sending heavy data if ack received
    uint32_t srvProcessedProgId = pl["processedProgramReqId"].toInt();
    if (srvProcessedProgId == m_controlState.programUpdateReqId && m_controlState.programUpdateReqId > 0) {
        // We received ACK for this specific ID. Stop sending the payload.
        // But DO NOT reset m_globalRequestId.
        m_controlState.programUpdateReqId = 0;
    }

    uint32_t srvProcessedCfgId = pl["processedConfigReqId"].toInt();
    if (srvProcessedCfgId == m_controlState.configUpdateReqId && m_controlState.configUpdateReqId > 0) {
        m_controlState.configUpdateReqId = 0;
    }

    // Config Sync
    uint32_t srvConfigVer = pl["configVersion"].toInt();
    if (srvConfigVer != m_controlState.ackConfigVersion && pl.contains("config")) {
        QJsonObject cfgPl = pl["config"].toObject();
        m_config.tools.clear();
        QJsonArray tools = cfgPl["tools"].toArray();
        for(auto v : tools) {
            QJsonObject t = v.toObject();
            HmiToolData tool; tool.id = t["id"].toInt(); tool.name = t["name"].toString();
            for(auto d : t["offset"].toArray()) tool.offset.append(d.toDouble());
            m_config.tools.append(tool);
        }
        m_config.bases.clear();
        QJsonArray bases = cfgPl["bases"].toArray();
        for(auto v : bases) {
            QJsonObject b = v.toObject();
            HmiBaseData base; base.id = b["id"].toInt(); base.name = b["name"].toString();
            for(auto d : b["offset"].toArray()) base.offset.append(d.toDouble());
            m_config.bases.append(base);
        }
        m_controlState.ackConfigVersion = srvConfigVer;
        emit settingsReceived(m_config);
    }

    // Trajectory Sync
    uint32_t srvTrajVer = pl["trajVersion"].toInt();
    if (srvTrajVer != m_controlState.ackTrajVersion) {
        // Even if trajectory is empty (cleared), we must process it to clear UI
        if (pl.contains("trajectory")) {
            QJsonObject trajObj = pl["trajectory"].toObject();

            QJsonArray pts = trajObj["points"].toArray();
            QVector<QVector3D> path;
            for(auto v : pts) {
                QJsonObject p = v.toObject();
                path.append(QVector3D(p["x"].toDouble(), p["y"].toDouble(), p["z"].toDouble()));
            }

            QJsonArray wpts = trajObj["waypoints"].toArray();
            QVector<QVector3D> waypoints;
            for(auto v : wpts) {
                QJsonObject p = v.toObject();
                waypoints.append(QVector3D(p["x"].toDouble(), p["y"].toDouble(), p["z"].toDouble()));
            }

            m_cachedTrajectory.path = path;
            m_cachedTrajectory.waypoints = waypoints;
            m_controlState.ackTrajVersion = srvTrajVer;

            emit trajectoryReceived(m_cachedTrajectory);
        }
    }

    // Program Sync
    uint32_t srvProgVer = pl["programVersion"].toInt();
    if (srvProgVer != m_controlState.ackProgramVersion && pl.contains("loadedProgram")) {
        QJsonObject progObj = pl["loadedProgram"].toObject();
        QVector<ProgramCommand> hmiProg;
        QJsonArray steps = progObj["steps"].toArray();
        for(const auto& sVal : steps) {
            QJsonObject s = sVal.toObject();
            QString code = s["commandCode"].toString();
            CommandType type = (s["type"].toInt() == 0) ? CommandType::Motion : CommandType::Logic;
            ProgramCommand cmd(type, code, "Remote Step");
            QVector<double> jts;
            for(const auto& jVal : s["targetJoints"].toArray()) jts.append(jVal.toDouble());
            cmd.params["Joints"] = QVariant::fromValue(jts);
            cmd.params["Speed"] = s["speed"].toDouble();
            hmiProg.append(cmd);
        }

        m_cachedProgram = hmiProg;
        m_controlState.ackProgramVersion = srvProgVer;

        emit programLoaded(hmiProg);
    }

    m_status.timestamp = QDateTime::currentMSecsSinceEpoch();
    emit stateChanged(m_status);
}

void RobotService::jogJointIncremental(int axis, double degrees) {
    if (!m_controlState.enableRealMode) {
        emit messageOccurred("JOG BLOCKED: SIMULATION MODE ACTIVE", true);
        return;
    }
    m_controlState.jogRequestId++;
    m_controlState.jogAxis = axis;
    m_controlState.jogIncrement = degrees;
}

void RobotService::moveHome() {
    m_controlState.programCommand = 4;
}

void RobotService::setMode(bool isReal) {
    if (m_status.motion.isMoving) {
        emit messageOccurred("CANNOT SWITCH MODE WHILE MOVING", true);
        return;
    }
    m_controlState.enableRealMode = isReal;
}

void RobotService::setSpeedOverride(int percent) { m_controlState.speedOverride = std::clamp(percent / 100.0, 0.0, 1.0); }
void RobotService::requestSettings() { m_controlState.ackConfigVersion = 0; }

void RobotService::setMonitorContext(int toolId, int baseId) {
    m_controlState.monitorToolId = toolId;
    m_controlState.monitorBaseId = baseId;
}

void RobotService::applySettings(const HmiSystemConfig &newConfig) {
    m_config = newConfig;
    // FIX: Use global counter
    m_controlState.configUpdateReqId = nextRequestId();

    m_controlState.newConfig.tools.clear();
    for(const auto &t : newConfig.tools) {
        Rdt::ToolData td; td.id = t.id; td.name = t.name.toStdString();
        for(int i=0; i<6; ++i) td.offset[i] = t.offset[i];
        m_controlState.newConfig.tools.push_back(td);
    }
    m_controlState.newConfig.bases.clear();
    for(const auto &b : newConfig.bases) {
        Rdt::ToolData td; td.id = b.id; td.name = b.name.toStdString();
        for(int i=0; i<6; ++i) td.offset[i] = b.offset[i];
        m_controlState.newConfig.bases.push_back(td);
    }
    m_controlState.newConfig.ipAddress = newConfig.network.controllerIp.toStdString();

    if (m_socket->state() == QAbstractSocket::UnconnectedState) {
        m_socket->connectToHost(m_config.network.controllerIp, m_config.network.controllerPort);
    }
}

void RobotService::startProgram(const QVector<ProgramCommand> &prog) {
    // FIX: Use global counter
    m_controlState.programUpdateReqId = nextRequestId();

    fillProgramData(prog, m_controlState.newProgram);
    m_cachedProgram = prog;
    m_controlState.programCommand = 1;
}

void RobotService::uploadProgram(const QVector<ProgramCommand> &program) {
    // FIX: Use global counter
    m_controlState.programUpdateReqId = nextRequestId();

    fillProgramData(program, m_controlState.newProgram);
    m_controlState.programCommand = 0;
}

void RobotService::fillProgramData(const QVector<ProgramCommand> &src, Rdt::ProgramData &dst) {
    dst.steps.clear();
    dst.name = "HMI_Upload";
    int idx = 0;
    for(const auto &cmd : src) {
        if (cmd.type == CommandType::Motion) {
            Rdt::ProgramStep step; step.id = idx++; step.type = 0;
            step.commandCode = cmd.code.toStdString();
            step.speed = cmd.params.value("Speed", 50.0).toDouble();

            QVector<double> joints;
            QVariant jVar = cmd.params["Joints"];

            if (jVar.canConvert<QVector<double>>()) {
                joints = jVar.value<QVector<double>>();
            } else if (jVar.canConvert<QVariantList>()) {
                QVariantList list = jVar.toList();
                for(const auto& v : list) joints.append(v.toDouble());
            }

            if (joints.isEmpty()) {
                qWarning() << "Failed to extract Joints for step" << idx;
                joints.fill(0.0, 6);
            }

            for(int i=0; i<6 && i<joints.size(); ++i) step.targetJoints[i] = joints[i];
            dst.steps.push_back(step);
        }
    }
}

void RobotService::stopProgram() { m_controlState.programCommand = 3; }
void RobotService::pauseProgram() { m_controlState.programCommand = 2; }
void RobotService::setPreviewStep(int stepIndex) { m_controlState.previewStepIndex = stepIndex; }
void RobotService::stopJog() { m_controlState.jogAxis = -1; m_controlState.jogIncrement = 0; }
void RobotService::connectToController(const QString &ip, int port) { Q_UNUSED(ip); Q_UNUSED(port); }
void RobotService::disconnectFromController() {}
void RobotService::setEStop(bool active) {
    if(active) m_controlState.setEStop = true;
    else m_controlState.resetEStop = true;
}
