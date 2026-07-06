#include "ProgramModel.h"

ProgramModel::ProgramModel(QObject *parent)
    : QAbstractListModel(parent) {}

int ProgramModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_commands.size();
}

QVariant ProgramModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_commands.size()) return QVariant();
    const ProgramCommand &cmd = m_commands.at(index.row());
    switch (role) {
    case TypeRole: return QVariant::fromValue(cmd.type);
    case CodeRole: return cmd.code;
    case NameRole: return cmd.name;
    case ParamsRole: return cmd.params;
    case IsActiveRole: return (index.row() == m_activeRow);
    default: return QVariant();
    }
}

bool ProgramModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    if (!index.isValid() || index.row() >= m_commands.size()) return false;
    ProgramCommand &cmd = m_commands[index.row()];
    if (role == NameRole) cmd.name = value.toString();
    else if (role == CodeRole) cmd.code = value.toString();
    else if (role == ParamsRole) cmd.params = value.toMap();
    else return false;
    emit dataChanged(index, index, {role});
    return true;
}

void ProgramModel::addCommand(const ProgramCommand &cmd) {
    beginInsertRows(QModelIndex(), m_commands.size(), m_commands.size());
    m_commands.append(cmd);
    endInsertRows();
}

void ProgramModel::insertCommand(int row, const ProgramCommand &cmd) {
    beginInsertRows(QModelIndex(), row, row);
    m_commands.insert(row, cmd);
    endInsertRows();
}

void ProgramModel::removeCommand(int row) {
    if (row < 0 || row >= m_commands.size()) return;
    beginRemoveRows(QModelIndex(), row, row);
    m_commands.removeAt(row);
    endRemoveRows();
    if (m_activeRow == row) m_activeRow = -1;
    else if (m_activeRow > row) m_activeRow--;
}

void ProgramModel::moveCommand(int from, int to) {
    if (from < 0 || from >= m_commands.size() || to < 0 || to >= m_commands.size() || from == to) return;
    beginMoveRows(QModelIndex(), from, from, QModelIndex(), to > from ? to + 1 : to);
    m_commands.move(from, to);
    endMoveRows();
}

void ProgramModel::setProgramPointer(int row) {
    int old = m_activeRow;
    m_activeRow = row;
    if (old >= 0 && old < m_commands.size()) emit dataChanged(index(old), index(old), {IsActiveRole});
    if (m_activeRow >= 0 && m_activeRow < m_commands.size()) emit dataChanged(index(m_activeRow), index(m_activeRow), {IsActiveRole});
}

ProgramCommand ProgramModel::getCommand(int row) const {
    return (row >= 0 && row < m_commands.size()) ? m_commands[row] : ProgramCommand();
}

QVector<ProgramCommand> ProgramModel::getProgram() const {
    return m_commands;
}

void ProgramModel::clear() {
    beginResetModel();
    m_commands.clear();
    m_activeRow = -1;
    endResetModel();
}
