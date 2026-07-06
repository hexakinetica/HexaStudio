#ifndef CYBERKEYBOARD_H
#define CYBERKEYBOARD_H

#include <QDialog>
#include <QLineEdit>
#include <QButtonGroup>
#include <QPoint>

class QLabel;
class QMouseEvent;

/**
 * @brief On-screen text-entry dialog for the touch pendant (no physical keyboard on the panel).
 *
 * Cursor-aware editor (NG 0.6.15): on-screen keys insert AT the cursor, arrow keys move it,
 * a physical keyboard types straight into the display (focus stays there - every button is
 * Qt::NoFocus). Frameless but draggable by any free area of its frame; opens centred on the
 * top-level window; styled from HexaTheme (industrial carbon), not a bespoke look.
 */
class CyberKeyboard : public QDialog
{
    Q_OBJECT
public:
    static QString getString(QWidget *parent, const QString &initialValue, const QString &placeholder, bool *ok = nullptr);
    explicit CyberKeyboard(QWidget *parent = nullptr);

protected:
    // Frameless-window drag: any press on a non-interactive area of the frame moves the dialog.
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private slots:
    void onKeyClicked(int id);
    void onEnter();
    void onCancel();
    void onSpace();
    void onBackspace();
    void onShift();
    void onCursorLeft();
    void onCursorRight();
    void onClear();

private:
    void setupUi();
    void updateKeys();

    QLineEdit *m_display;
    QLabel *m_title = nullptr;      // names what is being entered; part of the drag surface
    QButtonGroup *m_charGroup;
    bool m_isShifted = false;
    bool m_dragging = false;
    QPoint m_dragOffset;
};

#endif // CYBERKEYBOARD_H
