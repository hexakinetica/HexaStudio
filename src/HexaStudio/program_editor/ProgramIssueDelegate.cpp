// --- START OF FILE: HexaStudio/program_editor/ProgramIssueDelegate.cpp ---
#include "ProgramIssueDelegate.h"
#include "ProgramEditorModel.h"   // IssueRole

#include <QPainter>
#include <QColor>

namespace hexa {

void ProgramIssueDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const {
    // Draw the row exactly as the production delegate does first.
    ProgramDelegate::paint(painter, option, index);

    // Then overlay the validation marker. A model without IssueRole (e.g. the legacy ProgramModel)
    // returns an invalid QVariant; treat that as "no issue" so this delegate is safe to reuse and the
    // marker never appears spuriously. Severity encoding: -1 none, 0 Warning, 1 Error.
    const QVariant raw = index.data(ProgramEditorModel::IssueRole);
    const int severity = raw.isValid() ? raw.toInt() : ProgramEditorModel::NoIssue;
    if (severity == ProgramEditorModel::NoIssue) {
        return;
    }

    // The base delegate draws a 3px type-color bar at the left edge and a "◄" active-line arrow at the
    // right edge. Place this validation marker just to the right of the type bar so neither is obscured.
    constexpr int kMarkerWidth = 3;
    constexpr int kMarkerLeftInset = 6;   // clears the base 3px type bar (+ a 1px gap)
    constexpr int kMarkerVInset = 6;
    const QColor markerColor = (severity == ProgramEditorModel::ErrorIssue) ? QColor(Hexa::Colors::BadgeError)
                                                                            : QColor(Hexa::Colors::BadgeWarn);
    painter->save();
    painter->fillRect(QRect(option.rect.left() + kMarkerLeftInset, option.rect.top() + kMarkerVInset,
                            kMarkerWidth, option.rect.height() - 2 * kMarkerVInset),
                      markerColor);
    painter->restore();
}

} // namespace hexa
// --- END OF FILE: HexaStudio/program_editor/ProgramIssueDelegate.cpp ---
