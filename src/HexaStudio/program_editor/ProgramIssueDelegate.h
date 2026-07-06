// --- START OF FILE: HexaStudio/program_editor/ProgramIssueDelegate.h ---
/**
 * @file ProgramIssueDelegate.h
 * @brief ProgramDelegate that additionally draws a validation-severity marker from IssueRole.
 *
 * This is a module-local extension of the production ProgramDelegate. The base delegate is reused
 * unchanged (rows render identically); this subclass only overlays a thin left-edge marker whose
 * colour reflects ProgramEditorModel::IssueRole (Error = red, Warning = amber, none = nothing). It is
 * confined to the program_editor module so the production PanelLeft, which keeps the plain delegate,
 * is not affected.
 */
#ifndef HEXA_PROGRAM_ISSUE_DELEGATE_H
#define HEXA_PROGRAM_ISSUE_DELEGATE_H

#include "ProgramDelegate.h"   // production base delegate (panels/left)

namespace hexa {

class ProgramIssueDelegate : public ProgramDelegate {
public:
    explicit ProgramIssueDelegate(QObject* parent = nullptr) : ProgramDelegate(parent) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
};

} // namespace hexa

#endif // HEXA_PROGRAM_ISSUE_DELEGATE_H
// --- END OF FILE: HexaStudio/program_editor/ProgramIssueDelegate.h ---
