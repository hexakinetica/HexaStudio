#ifndef SETTINGSOVERLAY_H
#define SETTINGSOVERLAY_H

#include <QWidget>
#include <QPushButton>
#include <QListWidget>
#include <QScrollArea>
#include <QLabel>
#include <QStackedWidget>
#include "backend/BackendTypes.h"

class SettingsOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsOverlay(QWidget *parent = nullptr);

    // Load config into the editor (create a local copy)
    void setConfig(const HmiSystemConfig &config);

signals:
    void closeRequested();
    void applyRequested(const HmiSystemConfig &newConfig);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void onCategoryChanged(int row);
    void onApplyClicked();
    void onCancelClicked();

private:
    void setupUi();

    // UI Builders for specific pages
    QWidget* createNetworkEditor();
    QWidget* createLimitsEditor();
    QWidget* createToolsEditor();
    QWidget* createBasesEditor();

    // Helpers
    void updateInfoPanel(int categoryIndex);

    // UI Elements
    QListWidget *m_listCategories;
    QScrollArea *m_scrollEditor;
    QLabel *m_lblInfoTitle;
    QLabel *m_lblInfoText;

    QPushButton *m_btnApply;
    QPushButton *m_btnCancel;

    // Data
    HmiSystemConfig m_tempConfig; // Local copy for editing
};

#endif // SETTINGSOVERLAY_H
