#include "ProgramDelegate.h"
#include "ProgramModel.h"
#include <QPainter>
#include <QPainterPath>
#include <QDebug>

ProgramDelegate::ProgramDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

QSize ProgramDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    Q_UNUSED(option); Q_UNUSED(index);
    return QSize(-1, 50);
}

QColor ProgramDelegate::getTypeColor(CommandType type) const {
    // Row identity (boss decision, NG 0.6.14): MOTIONS are violet, COMMANDS (logic + IO) share the
    // cyan accent, comments stay muted - the operator scans the program by colour: violet = the
    // robot moves, cyan = the program acts without moving.
    switch (type) {
    case CommandType::Motion: return QColor(Hexa::Colors::Active);
    case CommandType::Logic:  return QColor(Hexa::Colors::Primary);
    case CommandType::IO:     return QColor(Hexa::Colors::Primary);
    case CommandType::Comment:return QColor(Hexa::Colors::TextMuted);
    default: return QColor(Hexa::Colors::TextMain);
    }
}

void ProgramDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    CommandType type = index.data(ProgramModel::TypeRole).value<CommandType>();
    QString code = index.data(ProgramModel::CodeRole).toString();
    QString name = index.data(ProgramModel::NameRole).toString();
    QVariantMap params = index.data(ProgramModel::ParamsRole).toMap();
    bool isActive = index.data(ProgramModel::IsActiveRole).toBool();
    bool isSelected = (option.state & QStyle::State_Selected);

    QRect rect = option.rect.adjusted(1, 1, -1, -1);
    QColor bgColor = QColor(Hexa::Colors::Surface);
    if (isSelected) bgColor = QColor(Hexa::Colors::StateHover);
    if (isActive) bgColor = QColor(Hexa::Colors::Execution).darker(300);

    if (!isSelected && !isActive) bgColor.setAlpha(100); else bgColor.setAlpha(200);

    QPainterPath path;
    path.addRoundedRect(rect, 2, 2);
    painter->fillPath(path, bgColor);

    QRect barRect = rect;
    barRect.setWidth(3);
    QColor typeColor = getTypeColor(type);
    if (isActive) typeColor = QColor(Hexa::Colors::Execution);
    painter->fillRect(barRect, typeColor);

    QRect topLineRect = rect.adjusted(10, 5, -10, 0);
    topLineRect.setHeight(20);

    painter->setPen(typeColor);
    painter->setFont(Hexa::Fonts::getInterface(10, true));
    QString displayCode = code;
    if (code == "IF" && params.contains("Subtype")) { displayCode = params["Subtype"].toString(); }
    painter->drawText(topLineRect, Qt::AlignVCenter | Qt::AlignLeft, displayCode);

    QRect nameRect = topLineRect.adjusted(50, 0, 0, 0);
    painter->setPen(QColor(Hexa::Colors::TextMain));
    painter->setFont(Hexa::Fonts::getInterface(11, false));
    painter->drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft, name);

    QString speedStr;
    if (params.contains("Speed")) speedStr += QString("Vel:%1%").arg(params["Speed"].toInt());
    painter->setPen(QColor(Hexa::Colors::TextMuted));
    painter->drawText(topLineRect, Qt::AlignVCenter | Qt::AlignRight, speedStr);

    QRect bottomLineRect = rect.adjusted(10, 25, -10, -5);
    painter->setFont(Hexa::Fonts::getMono(9));
    painter->setPen(QColor(Hexa::Colors::TextMuted));

    QString coordsStr;
    if (params.contains("Joints")) {
        QVector<double> joints = params["Joints"].value<QVector<double>>();
        coordsStr = "J: ";
        for(int i=0; i<joints.size(); ++i) {
            coordsStr += QString::number(joints[i], 'f', 1) + (i == joints.size() - 1 ? "" : ", ");
        }
    } else if (code == "IF") {
        QString subtype = params.value("Subtype").toString();
        if (subtype == "IF") coordsStr = "[" + params.value("Condition").toString() + "]";
    }
    painter->drawText(bottomLineRect, Qt::AlignVCenter | Qt::AlignLeft, coordsStr);

    if (isActive) {
        painter->setPen(QColor(Hexa::Colors::Execution));
        painter->setFont(Hexa::Fonts::getMono(12));
        QRect arrowRect = rect.adjusted(rect.width() - 20, 0, 0, 0);
        painter->drawText(arrowRect, Qt::AlignVCenter | Qt::AlignCenter, "◄");
    }

    painter->restore();
}
