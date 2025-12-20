/**
 * @file ProgramDelegate.h
 * @brief Custom Delegate for rendering program blocks.
 * @author HexaKinetica Team
 * @version 1.0
 *
 * This file defines a custom item delegate that renders each command in the
 * QListView not just as text, but as a stylized "block" with icons, parameter
 * details, and status indicators (active execution arrow).
 */

#ifndef PROGRAMDELEGATE_H
#define PROGRAMDELEGATE_H

#include <QStyledItemDelegate>
#include "../../styles/HexaTheme.h"
#include "ProgramData.h"

/**
 * @brief Custom painter for program list items.
 *
 * Renders commands with the "Cyberpunk/Sci-Fi" aesthetic:
 * - Color-coded bars based on command type (Motion=Cyan, Logic=Purple).
 * - Two-line layout (Code/Name on top, Parameters on bottom).
 * - "Active execution" highlight.
 */
class ProgramDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit ProgramDelegate(QObject *parent = nullptr);

    /**
     * @brief Paints the item.
     * @param painter The QPainter object.
     * @param option Style options (rect, state, etc.).
     * @param index The model index being painted.
     */
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    /**
     * @brief Defines the size of the item.
     * @details Returns a fixed height to accommodate two lines of text.
     */
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    /**
     * @brief Helper to determine the accent color based on command type.
     * @param type The command type.
     * @return QColor for the indicator bar.
     */
    QColor getTypeColor(CommandType type) const;
};

#endif // PROGRAMDELEGATE_H
