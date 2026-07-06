// --- START OF FILE: HexaStudio/program_editor/ProgramEditorModel.h ---
/**
 * @file ProgramEditorModel.h
 * @brief Thin Qt list-model adapter over ProgramBuilder for display in a QListView.
 *
 * The model holds NO program content of its own: it reads everything from ProgramBuilder (the single
 * source of truth) and stays in sync via the builder's granular signals. It is Qt Core only (no
 * widgets), so it remains unit-testable without a GUI.
 *
 * Two transient, view-only concerns live here (NOT in the builder), per the authoring/execution ADR:
 *  - the execution pointer (active running line) fed by controller feedback (IsActiveRole);
 *  - the validation severity per row (IssueRole), recomputed from ProgramBuilder::validate().
 *
 * Role values are intentionally identical to panels/left/ProgramModel so the existing ProgramDelegate
 * renders rows unchanged. They are duplicated here (not included) to avoid pulling ProgramModel's
 * QObject into AUTOMOC; keep them in sync with ProgramModel::Roles.
 */
#ifndef HEXA_PROGRAM_EDITOR_MODEL_H
#define HEXA_PROGRAM_EDITOR_MODEL_H

#include <QAbstractListModel>
#include <QVector>

#include "ProgramBuilder.h"
#include "ProgramModel.h"   // reuse the exact role values (single source of truth for ProgramDelegate)

namespace hexa {

class ProgramEditorModel : public QAbstractListModel {
    Q_OBJECT
public:
    // Role values are taken directly from panels/left/ProgramModel::Roles so the shared ProgramDelegate
    // renders identically. They are NOT re-declared by hand (removes the sync hazard); IssueRole is
    // appended after them.
    enum Roles {
        TypeRole     = ProgramModel::TypeRole,
        CodeRole     = ProgramModel::CodeRole,
        NameRole     = ProgramModel::NameRole,
        ParamsRole   = ProgramModel::ParamsRole,
        IsActiveRole = ProgramModel::IsActiveRole,
        IssueRole    = ProgramModel::IsActiveRole + 1
    };

    /// @brief Severity encoding carried by IssueRole (explicit, instead of bare -1/0/1 magic ints).
    enum IssueMarker { NoIssue = -1, WarningIssue = 0, ErrorIssue = 1 };

    explicit ProgramEditorModel(ProgramBuilder* builder, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

public slots:
    /// @brief Set the execution pointer (active running line) from controller feedback; -1 = none.
    void setActiveRow(int row);
    /// @brief Recompute the validation cache and refresh IssueRole for all rows.
    void refreshIssues();

private:
    void connectBuilder();
    void rebuildIssueCache();
    int  worstSeverityAt(int row) const; // -1 none, 0 warning, 1 error

    ProgramBuilder* m_builder = nullptr;  // not owned
    int m_activeRow = -1;
    QVector<ProgramIssue> m_issues;
};

} // namespace hexa

#endif // HEXA_PROGRAM_EDITOR_MODEL_H
// --- END OF FILE: HexaStudio/program_editor/ProgramEditorModel.h ---
