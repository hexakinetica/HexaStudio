// --- START OF FILE: HexaStudio/overlays/SettingsPanel.h ---
/**
 * @file SettingsPanel.h
 * @brief Standalone, RobotService-free settings overlay for the new-product overlays module.
 *
 * Module-owned re-implementation of the shipping SettingsOverlay (which stays untouched). It keeps
 * the shipping overlay's contract, so it drops into the same MainWindow wiring:
 *   - intent out: closeRequested / applyRequested(HmiSystemConfig) / halOverlayRequested;
 *   - feedback in: setConfig(HmiSystemConfig).
 * Deliberate contract deviation: the shipping calibrationOverlayRequested signal is DROPPED - the
 * calibration overlay was cancelled by the boss (2026-07-03).
 *
 * Differences vs the shipping overlay:
 *   - NO RobotService singleton: the config is PUSHED via setConfig by the host (the shipping
 *     overlay pulled RobotService::instance()->getConfig() in showEvent);
 *   - HAL RUNTIME is an explicit navigation button under the category list, not a fake list row
 *     that bounces the selection back (the shipping "trampoline" rows); the CALIBRATION entry is
 *     gone entirely - the calibration overlay was cancelled by the boss (calibration happens on the
 *     HAL panel, which already provides homing / set-zero / fine jog);
 *   - editors are themed: section cards with title captions instead of naked QGroupBox, one shared
 *     dark mono field style for all inputs, no ad-hoc inline widget styles;
 *   - the fixed 220 px INFO side column is removed: each editor carries a one-line muted caption;
 *   - categories are a typed enum (no magic row numbers);
 *   - studio-local endpoint persistence (QSettings) is NOT read here - merging workstation-local
 *     values into the config is the shell's job (cross-module note in the requirements).
 */
#ifndef HEXA_SETTINGS_PANEL_H
#define HEXA_SETTINGS_PANEL_H

#include <QWidget>
#include <QVector>
#include <functional>

#include "BackendTypes.h"   // HmiSystemConfig, HmiToolData, HmiBaseData, HmiAxisLimit

class QDoubleSpinBox;
class QFrame;
class QLabel;
class QListWidget;
class QMouseEvent;
class QPaintEvent;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace hexa {

class SettingsPanel : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPanel(QWidget* parent = nullptr);

public slots:
    void setConfig(const HmiSystemConfig& config);

signals:
    void closeRequested();
    void applyRequested(const HmiSystemConfig& newConfig);
    void halOverlayRequested();

protected:
    void mousePressEvent(QMouseEvent* event) override;   // blocks click-through to the app behind
    void paintEvent(QPaintEvent* event) override;        // dims the application behind the overlay

private slots:
    void onCategoryChanged(int row);
    void onApplyClicked();

private:
    // Real edit categories only. HAL / CALIB are separate overlays reached via explicit buttons.
    enum Category : int {
        CategoryNetwork = 0,
        CategoryLimits,
        CategoryTools,
        CategoryBases,
        CategoryRobotVisual,
        CategoryCount
    };

    void setupUi();
    void rebuildEditor(int category);
    QWidget* createNetworkEditor();
    QWidget* createLimitsEditor();
    QWidget* createToolsEditor();
    QWidget* createBasesEditor();
    // Shared editor for the structurally-identical Tool/Base frame lists (id/name/offset).
    template<class T>
    QWidget* createFrameEditor(QVector<T>& items, const QString& kindUpper, const QString& kindTitle);
    QWidget* createRobotVisualEditor();

    // Shared themed building blocks live in OverlayWidgets (used by every panel of this module).
    QDoubleSpinBox* createSpin(double min, double max, int decimals, double step, double value,
                               const std::function<void(double)>& setter);

    QListWidget* m_listCategories = nullptr;
    QScrollArea* m_scrollEditor = nullptr;
    QLabel* m_lblApplyStatus = nullptr;
    QPushButton* m_btnApply = nullptr;
    QPushButton* m_btnClose = nullptr;

    HmiSystemConfig m_tempConfig;   // staged copy; editors bind directly into it, APPLY emits it
    int m_lastCategory = CategoryNetwork;
};

} // namespace hexa

#endif // HEXA_SETTINGS_PANEL_H
// --- END OF FILE: HexaStudio/overlays/SettingsPanel.h ---
