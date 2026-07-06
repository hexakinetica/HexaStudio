// --- START OF FILE: HexaStudio/jog_control/FramePickerDialog.h ---
/**
 * @file FramePickerDialog.h
 * @brief Module-owned tool/base picker for the jog module (used INSTEAD of the shipping
 *        FrameSelectionDialog inside this module; the shipping dialog stays untouched).
 *
 * Same call contract as the shipping dialog (setToolData/getSelectedToolId, setBaseData/
 * getSelectedBaseId -> drop-in for the JogPanel call sites), different presentation:
 *   - all frames in a flat list: one tap selects and previews, a double tap confirms
 *     (the combo box needed open + pick + OK);
 *   - the frame offset in an instrument-style grid - translations X/Y/Z (mm) left, rotations
 *     Rx/Ry/Rz (deg) right, mono font - the same visual language as the panel's monitor card,
 *     instead of one wrapped text line;
 *   - no "Comment" row: it only mirrored the name (dead data);
 *   - frameless window on the application background, so the picker reads as part of the
 *     pendant UI rather than an OS dialog.
 */
#ifndef HEXA_FRAME_PICKER_DIALOG_H
#define HEXA_FRAME_PICKER_DIALOG_H

#include <QDialog>
#include <QVector>

#include "BackendTypes.h"   // HmiToolData, HmiBaseData

class QLabel;
class QListWidget;
class QShowEvent;

namespace hexa {

class FramePickerDialog : public QDialog {
    Q_OBJECT
public:
    explicit FramePickerDialog(QWidget* parent = nullptr);

    // Tool selection
    void setToolData(const QVector<HmiToolData>& tools, int currentId = -1);
    int getSelectedToolId() const;

    // Base selection
    void setBaseData(const QVector<HmiBaseData>& bases, int currentId = -1);
    int getSelectedBaseId() const;

protected:
    // Frameless dialogs do not auto-center: place the picker over the host window centre.
    void showEvent(QShowEvent* event) override;

private slots:
    void onCurrentRowChanged(int row);

private:
    void setupUi();
    void populateList(const QStringList& names, const QVector<int>& ids, int currentId);
    void showOffset(const QVector<double>& offset);
    void clearOffset();

    QLabel* m_title = nullptr;
    QListWidget* m_list = nullptr;
    QVector<QLabel*> m_offsetValues;   // X, Y, Z, Rx, Ry, Rz

    QVector<HmiToolData> m_tools;
    QVector<HmiBaseData> m_bases;
    bool m_isToolMode = true;
    int m_selectedId = -1;
};

} // namespace hexa

#endif // HEXA_FRAME_PICKER_DIALOG_H
// --- END OF FILE: HexaStudio/jog_control/FramePickerDialog.h ---
