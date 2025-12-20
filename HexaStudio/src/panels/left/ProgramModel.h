/**
 * @file ProgramModel.h
 * @brief Qt Item Model for the robot program list.
 * @author HexaKinetica Team
 * @version 1.0
 *
 * This file defines the data model that bridges the gap between the `QVector<ProgramCommand>`
 * data structure and the `QListView` UI component. It follows the standard Qt
 * Model/View architecture.
 */

#ifndef PROGRAMMODEL_H
#define PROGRAMMODEL_H

#include <QAbstractListModel>
#include <QVector>
#include "ProgramData.h"

/**
 * @brief A custom list model to manage and display robot program commands.
 *
 * This class adapts the `ProgramCommand` structure for display in a QListView.
 * It handles adding, removing, moving, and editing commands, and provides
 * custom roles for the `ProgramDelegate` to render the UI.
 */
class ProgramModel : public QAbstractListModel
{
    Q_OBJECT
public:
    /**
     * @brief Custom roles for accessing specific command data in the delegate.
     */
    enum Roles {
        TypeRole = Qt::UserRole + 1, ///< CommandType (Motion, Logic, etc.).
        CodeRole,                    ///< Short code string (e.g., "MOVJ").
        NameRole,                    ///< User-defined name.
        ParamsRole,                  ///< QVariantMap of parameters.
        IsActiveRole                 ///< Boolean indicating if this is the currently executing line.
    };

    explicit ProgramModel(QObject *parent = nullptr);

    // --- QAbstractListModel Interface ---

    /**
     * @brief Returns the number of commands in the program.
     */
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    /**
     * @brief Returns data for a specific index and role.
     * @param index The model index.
     * @param role The role identifier (see `Roles` enum).
     * @return Data variant.
     */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    /**
     * @brief Updates data at a specific index.
     * @return True if successful.
     */
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

    // --- Custom API ---

    /**
     * @brief Appends a new command to the end of the list.
     * @param cmd The command to add.
     */
    void addCommand(const ProgramCommand &cmd);

    /**
     * @brief Inserts a command at a specific position.
     * @param row Row index to insert at.
     * @param cmd The command to insert.
     */
    void insertCommand(int row, const ProgramCommand &cmd);

    /**
     * @brief Removes a command at a specific row.
     * @param row Index of the command to remove.
     */
    void removeCommand(int row);

    /**
     * @brief Moves a command from one position to another (Drag & Drop support).
     * @param from Source index.
     * @param to Destination index.
     */
    void moveCommand(int from, int to);

    /**
     * @brief Sets the "Program Pointer" (active line cursor).
     * @param row Index of the currently executing command. -1 to clear.
     */
    void setProgramPointer(int row);

    /**
     * @brief Gets the current program pointer index.
     */
    int getProgramPointer() const { return m_activeRow; }

    /**
     * @brief Retrieves a specific command object.
     * @param row Index of the command.
     * @return The ProgramCommand object.
     */
    ProgramCommand getCommand(int row) const;

    /**
     * @brief Returns the entire program as a vector.
     * @details Used for serializing or uploading the program to the controller.
     */
    QVector<ProgramCommand> getProgram() const;

    // --- File I/O ---

    /**
     * @brief Saves the current program to a JSON file.
     * @param filename Path to the file.
     * @return True on success.
     */
    bool saveToFile(const QString &filename);

    /**
     * @brief Loads a program from a JSON file, replacing the current one.
     * @param filename Path to the file.
     * @return True on success.
     */
    bool loadFromFile(const QString &filename);

    /**
     * @brief Clears all commands from the model.
     */
    void clear();

private:
    /// @brief Internal storage of program commands.
    QVector<ProgramCommand> m_commands;

    /// @brief Index of the currently executing command (-1 if none).
    int m_activeRow = -1;
};

#endif // PROGRAMMODEL_H
