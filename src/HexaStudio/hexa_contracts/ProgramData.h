#ifndef PROGRAMDATA_H
#define PROGRAMDATA_H

#include <QString>
#include <QVariantMap>
#include <QUuid>
#include <QVector>
#include <QMetaType>

enum class CommandType { Motion, Logic, IO, Comment };

// Program authoring DTO. Persistence is controller-side only (boss directive 2026-07-06): programs
// travel over the wire as NetProtocol::ProgramDataStruct (program_mapper::toRdt/toHmi); there is no
// JSON serialization of ProgramCommand on the pendant.
struct ProgramCommand {
    QUuid uuid;
    CommandType type;
    QString code;
    QString name;
    QVariantMap params;

    ProgramCommand() : uuid(QUuid::createUuid()) {}
    ProgramCommand(CommandType t, const QString &c, const QString &n, const QVariantMap &p = {})
        : uuid(QUuid::createUuid()), type(t), code(c), name(n), params(p) {}
};

Q_DECLARE_METATYPE(QVector<ProgramCommand>)

#endif // PROGRAMDATA_H
