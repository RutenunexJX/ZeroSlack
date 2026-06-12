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
class QWidget;

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
    AppMode getCurrentMode() const;
    void switchMode();
    void setMode(AppMode mode);

    // Key event handling
    bool handleKeyPress(QKeyEvent *event);
    bool handleKeyRelease(QKeyEvent *event);

signals:
    void modeChanged(AppMode newMode);
    void modeSwitchTriggered();
    void navigationToggleRequested();

private:
    void onShiftTimeout();
    void onDoubleClickTimeout();

    struct ModeState {
        AppMode currentMode = NormalMode;

        void toggle();
        bool set(AppMode mode);
        bool isNormal() const;
        bool isAlternate() const;
    };

    enum class ShiftReleaseAction {
        NotHandled,
        Handled,
        SwitchMode
    };

    struct ShiftGesture {
        bool pressed = false;
        QTimer* doubleClickTimer = nullptr;
        QTimer* releaseTimer = nullptr;
        int clickCount = 0;

        void init(ModeManager* owner);
        bool handlePress(QKeyEvent* event);
        ShiftReleaseAction handleRelease(QKeyEvent* event);
        void handleTimeout();
        void resetDoubleClick();
    };

    struct ShortcutSets {
        std::array<std::unique_ptr<QShortcut>, 10> normalModeShortcuts;
        std::array<std::unique_ptr<QShortcut>, 10> alternateModeShortcuts;

        void setup(ModeManager* owner, QWidget* parent);
        void updateForMode(const ModeState& modeState);
    };

    ModeState modeState;
    QTabWidget* tabWidget = nullptr;
    ShiftGesture shiftGesture;
    ShortcutSets shortcuts;

    // Helper methods
    void setupModeShortcuts(QWidget* parent);
    void applyModeStyles();
    void updateShortcutStates();
    void resetShiftDoubleClick();
};

#endif // MODEMANAGER_H
