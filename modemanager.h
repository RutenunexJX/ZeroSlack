#ifndef MODEMANAGER_H
#define MODEMANAGER_H

#include <QObject>
#include <QShortcut>
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
        NormalMode
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
    struct ModeState {
        AppMode currentMode = NormalMode;

        bool set(AppMode mode);
        bool isNormal() const;
    };

    struct ShortcutSets {
        std::array<std::unique_ptr<QShortcut>, 10> normalModeShortcuts;

        void setup(ModeManager* owner, QWidget* parent);
        void updateForMode(const ModeState& modeState);
    };

    ModeState modeState;
    QTabWidget* tabWidget = nullptr;
    ShortcutSets shortcuts;

    // Helper methods
    void setupModeShortcuts(QWidget* parent);
    void applyModeStyles();
    void updateShortcutStates();
};

#endif // MODEMANAGER_H
