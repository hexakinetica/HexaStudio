// --- START OF FILE: HexaStudio/status_bar/ConfirmDialog.h ---
/**
 * @file ConfirmDialog.h
 * @brief Module-owned, pendant-styled safety confirmation dialog (replaces QMessageBox::question
 *        inside this module; the OS-styled message box breaks the dark HMI look).
 *
 * Frameless window on the application background with a warning-coloured title, a plain message and
 * two buttons: CANCEL (safe default, focused) and a danger-styled confirm button with an explicit
 * action label (e.g. "SWITCH TO REAL"). Modal; centred over the host window.
 */
#ifndef HEXA_CONFIRM_DIALOG_H
#define HEXA_CONFIRM_DIALOG_H

#include <QDialog>

class QLabel;
class QPushButton;
class QShowEvent;

namespace hexa {

class ConfirmDialog : public QDialog {
    Q_OBJECT
public:
    explicit ConfirmDialog(QWidget* parent = nullptr);

    void setContent(const QString& title, const QString& message, const QString& confirmText);

    // One-call helper: returns true only if the operator explicitly confirmed.
    static bool confirm(QWidget* parent, const QString& title, const QString& message,
                        const QString& confirmText);

protected:
    // Frameless dialogs do not auto-center: place the dialog over the host window centre.
    void showEvent(QShowEvent* event) override;

private:
    void setupUi();

    QLabel* m_title = nullptr;
    QLabel* m_message = nullptr;
    QPushButton* m_btnConfirm = nullptr;
};

} // namespace hexa

#endif // HEXA_CONFIRM_DIALOG_H
// --- END OF FILE: HexaStudio/status_bar/ConfirmDialog.h ---
