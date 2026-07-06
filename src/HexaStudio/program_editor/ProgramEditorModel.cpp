// --- START OF FILE: HexaStudio/program_editor/ProgramEditorModel.cpp ---
#include "ProgramEditorModel.h"

namespace hexa {

ProgramEditorModel::ProgramEditorModel(ProgramBuilder* builder, QObject* parent)
    : QAbstractListModel(parent), m_builder(builder) {
    connectBuilder();
    rebuildIssueCache();
}

void ProgramEditorModel::connectBuilder() {
    if (!m_builder) {
        return;
    }

    // Full replacement (load/clear/undo/redo): reset the model regardless of origin. The reason only
    // matters to the panel (whether to re-upload), not to the display adapter.
    connect(m_builder, &ProgramBuilder::programReset, this, [this](ProgramResetReason) {
        beginResetModel();
        rebuildIssueCache();
        if (m_activeRow >= m_builder->stepCount()) {
            m_activeRow = -1;
        }
        endResetModel();
    });

    connect(m_builder, &ProgramBuilder::stepInserted, this, [this](int index) {
        beginInsertRows(QModelIndex(), index, index);
        rebuildIssueCache();
        endInsertRows();
    });

    connect(m_builder, &ProgramBuilder::stepRemoved, this, [this](int index) {
        beginRemoveRows(QModelIndex(), index, index);
        if (m_activeRow == index) {
            m_activeRow = -1;
        } else if (m_activeRow > index) {
            --m_activeRow;
        }
        rebuildIssueCache();
        endRemoveRows();
    });

    connect(m_builder, &ProgramBuilder::stepChanged, this, [this](int index) {
        // A single-step edit can change validation findings; recompute then refresh that row across all
        // roles (small teach programs make whole-cache recompute cheap).
        rebuildIssueCache();
        if (index >= 0 && index < rowCount()) {
            const QModelIndex idx = this->index(index);
            emit dataChanged(idx, idx);
        }
    });

    // The builder already moved its data by the time this fires; a reset keeps the adapter simple and
    // correct (a granular beginMoveRows path can be added later if selection preservation is needed).
    connect(m_builder, &ProgramBuilder::stepMoved, this, [this](int, int) {
        beginResetModel();
        rebuildIssueCache();
        endResetModel();
    });
}

int ProgramEditorModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid() || !m_builder) {
        return 0;
    }
    return m_builder->stepCount();
}

QVariant ProgramEditorModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || !m_builder) {
        return {};
    }
    const int row = index.row();
    if (row < 0 || row >= m_builder->stepCount()) {
        return {};
    }
    const ProgramCommand& cmd = m_builder->at(row);
    switch (role) {
        case TypeRole:     return QVariant::fromValue(cmd.type);
        case CodeRole:     return cmd.code;
        case NameRole:     return cmd.name;
        case ParamsRole:   return cmd.params;
        case IsActiveRole: return row == m_activeRow;
        case IssueRole:    return worstSeverityAt(row);
        default:           return {};
    }
}

void ProgramEditorModel::setActiveRow(int row) {
    if (row == m_activeRow) {
        return;
    }
    const int previous = m_activeRow;
    m_activeRow = row;
    auto notify = [this](int r) {
        if (r >= 0 && r < rowCount()) {
            const QModelIndex idx = this->index(r);
            emit dataChanged(idx, idx, {IsActiveRole});
        }
    };
    notify(previous);
    notify(m_activeRow);
}

void ProgramEditorModel::refreshIssues() {
    rebuildIssueCache();
    if (rowCount() > 0) {
        emit dataChanged(index(0), index(rowCount() - 1), {IssueRole});
    }
}

void ProgramEditorModel::rebuildIssueCache() {
    m_issues = m_builder ? m_builder->validate() : QVector<ProgramIssue>{};
}

int ProgramEditorModel::worstSeverityAt(int row) const {
    int worst = NoIssue;
    for (const ProgramIssue& issue : m_issues) {
        if (issue.stepIndex != row) {
            continue;
        }
        const int severity = (issue.severity == IssueSeverity::Error) ? ErrorIssue : WarningIssue;
        if (severity > worst) {
            worst = severity;
        }
    }
    return worst;
}

} // namespace hexa
// --- END OF FILE: HexaStudio/program_editor/ProgramEditorModel.cpp ---
