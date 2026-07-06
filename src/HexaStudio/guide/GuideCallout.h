// --- START OF FILE: HexaStudio/guide/GuideCallout.h ---
/**
 * @file GuideCallout.h
 * @brief Floating instruction card of the guided demo: step progress, title, body text,
 *        NEXT / EXIT buttons, abort display. Modeled on the diagnostics card.
 *
 * The callout is a dumb view: the shell owns it, positions it (side-column docking — it must
 * never overlap the native 3D viewport nor the status-bar E-Stop, GDE-REQ-0070) and routes its
 * two intents to the runner. EXIT is present in every mode at every step (GDE-REQ-0080).
 */
#ifndef HEXA_GUIDE_CALLOUT_H
#define HEXA_GUIDE_CALLOUT_H

#include <QFrame>

class QLabel;
class QProgressBar;
class QPushButton;

namespace hexa {

class GuideCallout : public QFrame {
    Q_OBJECT
public:
    explicit GuideCallout(QWidget* parent = nullptr);

    /// @brief Present one step. @p nextVisible shows the NEXT button (STEP / TRY-narration pacing);
    /// @p showMeVisible shows the SHOW ME button (TRY hands-on: press the target for the user).
    /// The hint line is hidden on every new step until escalated via showHint().
    void showStep(int stepIndex, int stepCount, const QString& title, const QString& body,
                  bool nextVisible, bool showMeVisible);

    /// @brief TRY idle escalation: reveal the step's "do this now" hint under the body.
    void showHint(const QString& hintText);

    /// @brief Present an abort/refusal reason (alert styling). Stays up until EXIT.
    void showAbort(const QString& reason);

signals:
    void nextRequested();
    void showMeRequested();
    void exitRequested();

private:
    void applyFrameStyle(bool alert);

    QLabel* m_progress = nullptr;
    QProgressBar* m_progressBar = nullptr;   // scenario progress: filled step / total steps
    QLabel* m_title = nullptr;
    QLabel* m_body = nullptr;
    QLabel* m_hint = nullptr;                // TRY: "do this now" prompt, hidden until escalated
    QPushButton* m_next = nullptr;
    QPushButton* m_showMe = nullptr;         // TRY: press the awaited target for the user
    QPushButton* m_exit = nullptr;
};

} // namespace hexa

#endif // HEXA_GUIDE_CALLOUT_H
// --- END OF FILE: HexaStudio/guide/GuideCallout.h ---
