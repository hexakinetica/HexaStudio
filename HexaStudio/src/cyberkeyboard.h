#ifndef CYBERKEYBOARD_H
#define CYBERKEYBOARD_H

#include <QDialog>
#include <QLineEdit>
#include <QButtonGroup>

class CyberKeyboard : public QDialog
{
    Q_OBJECT
public:
    // Статический метод для вызова
    static QString getString(QWidget *parent, const QString &initialValue, const QString &placeholder, bool *ok = nullptr);

    explicit CyberKeyboard(QWidget *parent = nullptr);

private slots:
    void onKeyClicked(int id);
    void onEnter();
    void onCancel();
    void onSpace();
    void onBackspace();
    void onShift();

private:
    void setupUi();
    void updateKeys(); // Обновляет текст кнопок (Shift/Unshift)

    QLineEdit *m_display;
    QButtonGroup *m_charGroup; // Группа для букв/цифр
    bool m_isShifted = false;
};

#endif // CYBERKEYBOARD_H
