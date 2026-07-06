#ifndef PROGRAMMODEL_H
#define PROGRAMMODEL_H

#include <QAbstractListModel>
#include <QVector>
#include "ProgramData.h"

class ProgramModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles { TypeRole = Qt::UserRole + 1, CodeRole, NameRole, ParamsRole, IsActiveRole };

    explicit ProgramModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

    void addCommand(const ProgramCommand &cmd);
    void insertCommand(int row, const ProgramCommand &cmd);
    void removeCommand(int row);
    void moveCommand(int from, int to);
    void setProgramPointer(int row);
    int getProgramPointer() const { return m_activeRow; }
    ProgramCommand getCommand(int row) const;
    QVector<ProgramCommand> getProgram() const;

    void clear();

private:
    QVector<ProgramCommand> m_commands;
    int m_activeRow = -1;
};

#endif // PROGRAMMODEL_H
