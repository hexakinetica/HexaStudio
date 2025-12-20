/**
 * @file ProgramDelegate.cpp
 * @brief Implementation of the ProgramDelegate.
 */

#include "ProgramDelegate.h"
#include "ProgramModel.h"
#include <QPainter>
#include <QPainterPath>
#include <QDebug>

ProgramDelegate::ProgramDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

QSize ProgramDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    Q_UNUSED(option); Q_UNUSED(index);
    // Fixed height to fit title and details
    return QSize(-1, 50);
}

QColor ProgramDelegate::getTypeColor(CommandType type) const {
    switch (type) {
    case CommandType::Motion: return QColor(Hexa::Colors::Primary);
    case CommandType::Logic:  return QColor(Hexa::Colors::Active);
    case CommandType::IO:     return QColor(Hexa::Colors::Accent);
    case CommandType::Comment:return QColor(Hexa::Colors::TextMuted);
    default: return QColor(Hexa::Colors::TextMain);
    }
}

void ProgramDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    // 1. Retrieve Data
    CommandType type = index.data(ProgramModel::TypeRole).value<CommandType>();
    QString code = index.data(ProgramModel::CodeRole).toString();
    QString name = index.data(ProgramModel::NameRole).toString();
    QVariantMap params = index.data(ProgramModel::ParamsRole).toMap();
    bool isActive = index.data(ProgramModel::IsActiveRole).toBool();
    bool isSelected = (option.state & QStyle::State_Selected);

    // 2. Background
    QRect rect = option.rect.adjusted(1, 1, -1, -1);
    QColor bgColor = QColor(Hexa::Colors::Surface);
    if (isSelected) bgColor = QColor(Hexa::Colors::StateHover);
    if (isActive) bgColor = QColor(Hexa::Colors::Execution).darker(300);

    // Transparent base if not selected/active
    if (!isSelected && !isActive) bgColor.setAlpha(100); else bgColor.setAlpha(200);

    QPainterPath path;
    path.addRoundedRect(rect, 2, 2);
    painter->fillPath(path, bgColor);

    // 3. Status Bar (Color Strip)
    QRect barRect = rect;
    barRect.setWidth(3);
    QColor typeColor = getTypeColor(type);
    if (isActive) typeColor = QColor(Hexa::Colors::Execution);
    painter->fillRect(barRect, typeColor);

    // 4. Text - Line 1 (Code, Name, Speed)
    QRect topLineRect = rect.adjusted(10, 5, -10, 0);
    topLineRect.setHeight(20);

    // Code (e.g., IF, MOVJ)
    painter->setPen(typeColor);
    painter->setFont(Hexa::Fonts::getInterface(10, true));
    QString displayCode = code;
    if (code == "IF" && params.contains("Subtype")) { displayCode = params["Subtype"].toString(); }
    painter->drawText(topLineRect, Qt::AlignVCenter | Qt::AlignLeft, displayCode);

    // Name
    QRect nameRect = topLineRect.adjusted(50, 0, 0, 0);
    painter->setPen(QColor(Hexa::Colors::TextMain));
    painter->setFont(Hexa::Fonts::getInterface(11, false));
    painter->drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft, name);

    // Speed Info (Right Aligned)
    QString speedStr;
    if (params.contains("Speed")) speedStr += QString("Vel:%1%").arg(params["Speed"].toInt());
    painter->setPen(QColor(Hexa::Colors::TextMuted));
    painter->drawText(topLineRect, Qt::AlignVCenter | Qt::AlignRight, speedStr);

    // 5. Text - Line 2 (Details/Coordinates)
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

    // 6. Active Execution Indicator (Arrow)
    if (isActive) {
        painter->setPen(QColor(Hexa::Colors::Execution));
        painter->setFont(Hexa::Fonts::getMono(12));
        QRect arrowRect = rect.adjusted(rect.width() - 20, 0, 0, 0);
        painter->drawText(arrowRect, Qt::AlignVCenter | Qt::AlignCenter, "◄");
    }

    painter->restore();
}
