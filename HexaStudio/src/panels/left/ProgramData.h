/**
 * @file ProgramData.h
 * @brief Defines data structures for the Visual Program Editor.
 *
 * This file contains the logic for the "Blockly-like" programming model used
 * in PanelLeft. It defines the commands (Motion, Logic, IO) and handles
 * JSON serialization for storage and network transfer.
 */

#ifndef PROGRAMDATA_H
#define PROGRAMDATA_H

#include <QString>
#include <QVariantMap>
#include <QUuid>
#include <QJsonObject>
#include <QJsonArray>
#include <QVector>
#include <QMetaType>
#include <QDebug>

/**
 * @brief Categorization of program commands.
 * Determines the visual style and editing interface in PanelLeft.
 */
enum class CommandType {
    Motion, ///< Movement commands (PTP, LIN, CIRC).
    Logic,  ///< Flow control (IF, LOOP, WAIT).
    IO,     ///< Input/Output operations (SET DO, WAIT DI).
    Comment ///< Non-executable comments.
};

/**
 * @brief A single executable instruction in the robot program.
 *
 * Represents one line/block in the visual editor. Contains all parameters
 * needed to execute the command on the controller.
 */
struct ProgramCommand {
    /// @brief Unique identifier for the command instance.
    QUuid uuid;

    /// @brief Category of the command.
    CommandType type;

    /// @brief Short code displayed in the UI icon (e.g., "MOVJ", "IF").
    QString code;

    /// @brief User-editable name or description.
    QString name;

    /**
     * @brief Dynamic parameters for the command.
     * @details Key-Value pairs. Examples:
     * - "Speed" (int): Velocity percentage.
     * - "Joints" (QVector<double>): Target joint angles.
     * - "Condition" (QString): Logic expression for IF statements.
     */
    QVariantMap params;

    /// @brief Default constructor creating a unique UUID.
    ProgramCommand() : uuid(QUuid::createUuid()) {}

    /**
     * @brief Parameterized constructor.
     * @param t Type of command.
     * @param c Short code.
     * @param n Name/Description.
     * @param p Parameters map.
     */
    ProgramCommand(CommandType t, const QString &c, const QString &n, const QVariantMap &p = {})
        : uuid(QUuid::createUuid()), type(t), code(c), name(n), params(p) {}

    /**
     * @brief Serializes the command to a JSON object.
     * @details Handles special conversion for QVector<double> (Joints/TCP)
     * which QJsonValue doesn't support directly.
     * @return QJsonObject representing the command.
     */
    QJsonObject toJson() const {
        QJsonObject json;
        json["uuid"] = uuid.toString();
        json["type"] = static_cast<int>(type);
        json["code"] = code;
        json["name"] = name;

        QJsonObject jsonParams;
        for(auto it = params.begin(); it != params.end(); ++it) {
            if (it.value().canConvert<QVector<double>>()) {
                QVector<double> vec = it.value().value<QVector<double>>();
                QJsonArray arr;
                for(double v : vec) arr.append(v);
                jsonParams[it.key()] = arr;
            } else {
                jsonParams[it.key()] = QJsonValue::fromVariant(it.value());
            }
        }
        json["params"] = jsonParams;
        return json;
    }

    /**
     * @brief Deserializes a command from a JSON object.
     * @details Reconstructs QVector<double> for "Joints" and "TcpPose" keys.
     * @param json The source JSON object.
     * @return A valid ProgramCommand.
     */
    static ProgramCommand fromJson(const QJsonObject &json) {
        ProgramCommand cmd;
        if (json.contains("uuid")) cmd.uuid = QUuid::fromString(json["uuid"].toString());
        cmd.type = static_cast<CommandType>(json["type"].toInt());
        cmd.code = json["code"].toString();
        cmd.name = json["name"].toString();

        QVariantMap rawParams = json["params"].toObject().toVariantMap();
        for(auto it = rawParams.begin(); it != rawParams.end(); ++it) {
            if (it.key() == "Joints" || it.key() == "TcpPose") {
                // JSON Arrays come as QVariantList in toVariantMap()
                if (it.value().canConvert<QVariantList>()) {
                    QVariantList list = it.value().toList();
                    QVector<double> vec;
                    for(const auto& v : list) vec.append(v.toDouble());
                    cmd.params[it.key()] = QVariant::fromValue(vec);
                }
            } else {
                cmd.params[it.key()] = it.value();
            }
        }
        return cmd;
    }
};

Q_DECLARE_METATYPE(QVector<ProgramCommand>)

#endif // PROGRAMDATA_H
