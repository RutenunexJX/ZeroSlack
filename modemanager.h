#ifndef MODEMANAGER_H
#define MODEMANAGER_H

#include <QObject>
#include <QShortcut>
#include <QTimer>
#include <QKeyEvent>
#include <memory>
#include <array>

class QTabWidget;
class MainWindow;

class ModeManager : public QObject
{
    Q_OBJECT

public:
    enum AppMode {
        NormalMode,
        AlternateMode
    };

    explicit ModeManager(QTabWidget* tabWidget, QObject *parent = nullptr);
    ~ModeManager();

    // Mode management
    AppMode getCurrentMode() const { return currentMode; }
    void switchMode();
    void setMode(AppMode mode);

    // Key event handling
    bool handleKeyPress(QKeyEvent *event);
    bool handleKeyRelease(QKeyEvent *event);

signals:
    void modeChanged(AppMode newMode);
    void modeSwitchTriggered();
    void navigationToggleRequested();

private slots:
    void onShiftTimeout();
    void onDoubleClickTimeout();

private:
    AppMode currentMode = NormalMode;
    QTabWidget* tabWidget;

    // Shift detection for mode switching
    bool shiftPressed = false;
    QTimer* shiftDoubleClickTimer;
    QTimer* shiftReleaseTimer;
    int shiftClickCount = 0;
    static const int DOUBLE_CLICK_INTERVAL = 300;
    static const int SHIFT_TIMEOUT = 1000;

    // Shortcuts
    std::array<std::unique_ptr<QShortcut>, 10> normalModeShortcuts;
    std::array<std::unique_ptr<QShortcut>, 10> alternateModeShortcuts;

    // Helper methods
    void setupModeShortcuts(QWidget* parent);
    void applyModeStyles();
    void updateShortcutStates();
    void resetShiftDoubleClick();
};

#endif // MODEMANAGER_H
