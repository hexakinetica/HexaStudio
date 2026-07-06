// --- START OF FILE: HexaStudio/app_shell/ShellWindow.h ---
/**
 * @file ShellWindow.h
 * @brief Composition root + mediator of the new product: assembles the module panels into the
 *        application window and owns ALL cross-module wiring.
 *
 * Mediator rules (Simplicity Mandate rule 6 - no hidden data flow):
 *   - every connect between a panel and the backend lives in ONE method (connectBackend) and is
 *     readable top-to-bottom: panel intent -> backend command, backend feedback -> panel slots;
 *   - feature modules never see each other or the backend type - the shell fans feedback out and
 *     routes intents in;
 *   - shell-owned behaviours (the pieces the shipping MainWindow owned) stay here, explicit:
 *     name -> id mapping for jog/monitor context (the shell holds the last received config),
 *     the GO HOME confirmation + home-program construction, the E-Stop TOGGLE for the top bar and
 *     diagnostics (reads the last received status), overlay navigation (settings / diagnostics /
 *     HAL show-hide), and the application version fed to the status bar About box.
 *
 * Layout mirrors the shipping app: status bar on top; splitter with the program editor (left),
 * the 3D viewport (centre - the viewport3d module, with a view-preset toolbar below it), and the
 * jog panel (right). Settings/HAL are full-window overlays; diagnostics is a card anchored under
 * the top-bar status indicator. The viewport is hidden while an overlay is open (it hosts a native
 * window that would otherwise render above the overlay).
 */
#ifndef HEXA_SHELL_WINDOW_H
#define HEXA_SHELL_WINDOW_H

#include <QPointer>
#include <QWidget>

#include "BackendTypes.h"   // HmiRobotStatus, HmiSystemConfig

class QEvent;
class QFrame;
class QLabel;
class QPushButton;
class QResizeEvent;
class QSplitter;

namespace hexa {

class BackendClient;
class DiagnosticsPanel;
class GuideCallout;
class GuidePanel;
class GuideRunner;
class HalPanel;
class JogPanel;
class ProgramEditorPanel;
class SettingsPanel;
class StatusBarPanel;
class ViewportPanel;

class ShellWindow : public QWidget {
    Q_OBJECT
public:
    explicit ShellWindow(const QString& appVersion, QWidget* parent = nullptr);

    // ALL wiring in one visible place. Call exactly once, before showing the window.
    void connectBackend(BackendClient& backend);

    // Panel access for the bench selftest (drive REAL widgets across module boundaries).
    StatusBarPanel& statusBar() { return *m_statusBar; }
    JogPanel& jogPanel() { return *m_jogPanel; }
    ProgramEditorPanel& programEditor() { return *m_programEditor; }
    SettingsPanel& settingsPanel() { return *m_settings; }
    DiagnosticsPanel& diagnosticsPanel() { return *m_diagnostics; }
    HalPanel& halPanel() { return *m_hal; }

protected:
    void resizeEvent(QResizeEvent* event) override;   // overlays track the window size
    void changeEvent(QEvent* event) override;         // window state -> full-screen button label

private:
    void setupUi();
    // Guided demo (guide module): THE target registry — every widget a scenario may address,
    // resolved through explicit panel accessors in one readable block (never findChild) — and
    // the guide wiring (panel intents -> runner, runner feedback -> panel + callout), kept as
    // its own method next to connectBackend for the same top-to-bottom readability.
    void registerGuideTargets();
    void connectGuide();
    // Dock the guide callout on the side column opposite the target: the callout must never
    // overlap the native 3D viewport (it would be overdrawn) nor the status-bar E-Stop.
    void positionGuideCallout(QWidget* target);
    // Position the bold highlight frame OVER the current target (a mouse-transparent overlay, so
    // it never touches the target's own stylesheet — survives self-restyling widgets like the jog
    // ENABLE button). Null target hides it.
    void positionGuideHighlight(QWidget* target);
    void toggleFullScreen();   // both directions; used by F11 and the UI-tab on-screen control
    // Investor/pitch view: hides the side panel cards, leaving the 3D scene + status bar. Driven by
    // the checkable PRESENTATION button on the scene toolbar (which stays visible as the exit path).
    void setPresentationMode(bool active);
    void positionDiagnostics();
    int lookupToolId(const QString& toolName) const;
    int lookupBaseId(const QString& baseName) const;

    // The viewport hosts a NATIVE window (QQuickView container), which always renders above sibling
    // widgets - a full-window overlay would be punched through by the 3D view. The shell therefore
    // hides the viewport while any overlay is open; called at every overlay show/hide site.
    void syncViewportObscuring();

    // Feature module panels (owned via Qt parenting).
    StatusBarPanel* m_statusBar = nullptr;
    ProgramEditorPanel* m_programEditor = nullptr;
    JogPanel* m_jogPanel = nullptr;
    QSplitter* m_splitter = nullptr;
    ViewportPanel* m_viewport = nullptr;
    QWidget* m_viewToolbar = nullptr;   // TOP/FRONT/SIDE/ISO/FIT presets + PRESENTATION, under the viewport
    QWidget* m_leftCard = nullptr;      // program editor card (hidden in presentation mode)
    QWidget* m_rightCard = nullptr;     // jog panel card (hidden in presentation mode)

    // Guided demo (guide module): chooser page (injected into the program editor's stack),
    // engine and floating callout — cross-module logic belongs to the mediator.
    GuidePanel* m_guidePanel = nullptr;
    GuideRunner* m_guideRunner = nullptr;
    GuideCallout* m_guideCallout = nullptr;
    QFrame* m_guideHighlight = nullptr;       // bold violet frame drawn over the current target
    QPointer<QWidget> m_guideCalloutTarget;   // current step's target: re-dock on window resize
    // The one view preset a guide scenario addresses (ISO framing); assigned where the toolbar
    // loop creates the buttons. The other presets are not guide targets.
    QPushButton* m_btnViewIso = nullptr;

    // Overlays (children of the window, resized with it).
    SettingsPanel* m_settings = nullptr;
    DiagnosticsPanel* m_diagnostics = nullptr;
    HalPanel* m_hal = nullptr;

    // Shell-owned state: the last received status/config back the mediator behaviours
    // (E-Stop toggle, name->id mapping). Display state only - never a command source of truth.
    HmiRobotStatus m_lastStatus;
    HmiSystemConfig m_lastConfig;
    QString m_appVersion;
    bool m_backendConnected = false;
};

} // namespace hexa

#endif // HEXA_SHELL_WINDOW_H
// --- END OF FILE: HexaStudio/app_shell/ShellWindow.h ---
