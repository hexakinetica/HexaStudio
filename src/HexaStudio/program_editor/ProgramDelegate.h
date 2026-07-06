#ifndef PROGRAMDELEGATE_H
#define PROGRAMDELEGATE_H

#include <QStyledItemDelegate>
#include "HexaTheme.h"   // hexa_ui_kit (module-owned theme)
#include "ProgramData.h"

class ProgramDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit ProgramDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    QColor getTypeColor(CommandType type) const;
};

#endif // PROGRAMDELEGATE_H
