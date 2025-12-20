/**
 * @file HexaWidgets.h
 * @brief Factory class for creating styled UI components.
 * @author HexaKinetica Team
 * @version 1.0
 *
 * This file defines the `HexaWidgets` factory, which provides static methods to create
 * standardized UI elements (Buttons, Labels, Panels) compliant with the
 * Sci-Fi/Cyberpunk design language defined in `HexaTheme.h`.
 * It also contains the `HexaToggle`, a custom animated switch widget.
 */

#ifndef HEXAWIDGETS_H
#define HEXAWIDGETS_H

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QComboBox>
#include <QAbstractButton>
#include "HexaTheme.h"

/**
 * @brief Custom animated toggle switch (ON/OFF).
 *
 * Replaces the standard QCheckBox with a sliding toggle inspired by mobile UI,
 * but styled for the industrial dark theme.
 */
class HexaToggle : public QAbstractButton {
    Q_OBJECT
    /// @brief Animation property for the thumb position (0-100).
    Q_PROPERTY(int offset READ offset WRITE setOffset)
public:
    explicit HexaToggle(QWidget *parent = nullptr);

    /**
     * @brief Recommended size for the widget.
     */
    QSize sizeHint() const override;

    /**
     * @brief Getter for the animation offset.
     */
    int offset() const { return m_offset; }

    /**
     * @brief Setter for the animation offset. Triggers repaint.
     */
    void setOffset(int o) { m_offset = o; update(); }

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

    /**
     * @brief Triggered when checked state changes. Starts the animation.
     */
    void checkStateSet() override;

private:
    int m_offset; ///< Current position of the toggle thumb (0=Left/Off, 100=Right/On).
};

/**
 * @brief Static factory for creating consistent UI widgets.
 *
 * Instead of subclassing every widget, this class provides factory methods that
 * instantiate standard Qt widgets and apply specific stylesheets and properties
 * (fixed sizes, cursors, fonts) defined in `HexaTheme`.
 */
class HexaWidgets
{
public:
    // --- PANELS ---

    /**
     * @brief Creates a main container panel with a border and background.
     */
    static QFrame* createMainPanel(QWidget* parent = nullptr);

    /**
     * @brief Creates a vertical separator line.
     */
    static QFrame* createSeparatorV();

    /**
     * @brief Creates a horizontal separator line.
     */
    static QFrame* createSeparatorH();

    /**
     * @brief Creates a thick vertical separator for grouping sections.
     */
    static QFrame* createSectionSeparator();

    // --- BUTTONS ---

    /**
     * @brief Creates a standard primary button.
     * @param text Button label.
     * @param w Fixed width (0 for auto).
     * @param h Fixed height (0 for default).
     */
    static QPushButton* createButtonStd(const QString& text, QWidget* parent = nullptr, int w = 0, int h = 0);

    /**
     * @brief Creates a smaller secondary button.
     */
    static QPushButton* createButtonSm(const QString& text, QWidget* parent = nullptr, int w = 0, int h = 0);

    /**
     * @brief Creates a button styled for destructive actions (e.g., Delete, Stop).
     */
    static QPushButton* createButtonDanger(const QString& text, QWidget* parent = nullptr, int w = 0, int h = 0);

    /**
     * @brief Creates a square button suitable for icons or single characters (+/-).
     */
    static QPushButton* createButtonIcon(const QString& text, QWidget* parent = nullptr, int w = 0, int h = 0);

    // --- INPUTS ---

    /**
     * @brief Creates a styled QComboBox.
     */
    static QComboBox* createComboBox(QWidget* parent = nullptr);

    // --- LABELS ---

    /**
     * @brief Creates a large header label with the 'Michroma' font.
     */
    static QLabel* createLabelHeader(const QString& text);

    /**
     * @brief Creates a section title label (smaller header).
     */
    static QLabel* createLabelSectionTitle(const QString& text);

    /**
     * @brief Creates a label for axis names (A1, X, etc.).
     */
    static QLabel* createLabelAxis(const QString& text);

    /**
     * @brief Creates a status label with a colored left border indicator.
     * @see updateStatusLabel
     */
    static QLabel* createLabelStatus(const QString& text);

    /**
     * @brief Creates a standard UI text label.
     */
    static QLabel* createLabelText(const QString& text);

    /**
     * @brief Creates a monospaced label for numeric data.
     */
    static QLabel* createLabelData(const QString& text);

    /**
     * @brief Creates a clickable text button used as a section title/toggle.
     */
    static QPushButton* createTitleButton(const QString& text, QWidget* parent = nullptr);

    // --- LOGIC ---

    /**
     * @brief Updates the style of a status label based on the state.
     * @param lbl Pointer to the label created via createLabelStatus.
     * @param state The state (Success, Error, Warning, Active).
     */
    static void updateStatusLabel(QLabel* lbl, Hexa::State state);

    /**
     * @brief Toggles the visual state of a danger button (e.g., E-Stop active vs inactive).
     */
    static void updateButtonDangerState(QPushButton* btn, bool isActive);
};

#endif // HEXAWIDGETS_H
