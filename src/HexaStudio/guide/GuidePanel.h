// --- START OF FILE: HexaStudio/guide/GuidePanel.h ---
/**
 * @file GuidePanel.h
 * @brief Dumb guided-demo chooser page: scenario card list, PLAY/STEP pace chips, START/STOP,
 *        outcome line. Emits intents only; receives progress feedback through slots.
 *
 * Hosted as a page inside the program editor's main stack (injected by the shell through
 * ProgramEditorPanel::installGuidePage — the editor never sees this module, GDE-REQ-0090).
 * Sized for the ~230 px page width of the 300 px left column: scenarios render as fixed-height
 * two-line cards (boss decision), not table rows.
 */
#ifndef HEXA_GUIDE_PANEL_H
#define HEXA_GUIDE_PANEL_H

#include <QFrame>
#include <QVector>

#include "GuideTypes.h"

class QButtonGroup;
class QLabel;
class QMouseEvent;
class QProgressBar;
class QPushButton;

namespace hexa {

/// @brief One clickable two-line scenario card (name + MOTION badge / duration + description).
class GuideScenarioCard : public QFrame {
    Q_OBJECT
public:
    explicit GuideScenarioCard(const GuideScenario& scenario, QWidget* parent = nullptr);

    const QString& scenarioId() const { return m_scenarioId; }
    void setSelected(bool selected);

signals:
    void cardClicked(const QString& scenarioId);

protected:
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QString m_scenarioId;
};

class GuidePanel : public QWidget {
    Q_OBJECT
public:
    explicit GuidePanel(QWidget* parent = nullptr);

signals:
    void guideStartRequested(const QString& scenarioId, GuideMode mode);
    void guideStopRequested();

public slots:
    void onScenarioStarted(const QString& scenarioId, GuideMode mode, int stepCount);
    void onStepEntered(int stepIndex, int stepCount);
    void onScenarioEnded(GuideOutcome outcome, const QString& detail);

private slots:
    void onCardClicked(const QString& scenarioId);
    void onStartStopClicked();

private:
    void setupUi();
    void setRunningUi(bool running);
    void setStatus(const QString& text, bool alert);
    GuideMode selectedMode() const;

    QVector<GuideScenarioCard*> m_cards;
    QString m_selectedScenarioId;
    QButtonGroup* m_modeGroup = nullptr;
    QPushButton* m_btnStartStop = nullptr;
    QLabel* m_lblStatus = nullptr;
    QProgressBar* m_progressBar = nullptr;   // scenario progress; visible only while running
    bool m_running = false;
    // The ACTIVE run's mode as reported by the runner — the chips only hold the NEXT selection
    // (a host may start the runner directly, e.g. the bench; the status line must not guess).
    GuideMode m_runningMode = GuideMode::Play;
};

} // namespace hexa

#endif // HEXA_GUIDE_PANEL_H
// --- END OF FILE: HexaStudio/guide/GuidePanel.h ---
