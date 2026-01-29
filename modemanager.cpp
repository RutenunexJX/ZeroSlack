#include "modemanager.h"
#include <QTabWidget>
#include <QTabBar>
#include <QKeyEvent>
#include <QFont>
#include "mycodeeditor.h"
#include "mainwindow.h"

ModeManager::ModeManager(QTabWidget* tabWidget, QObject *parent)
    : QObject(parent), tabWidget(tabWidget)
{
    if (!tabWidget) {
        return;
    }

    // Initialize timers
    shiftReleaseTimer = new QTimer(this);
    shiftReleaseTimer->setSingleShot(true);
    shiftReleaseTimer->setInterval(SHIFT_TIMEOUT);
    connect(shiftReleaseTimer, &QTimer::timeout, this, &ModeManager::onShiftTimeout);

    shiftDoubleClickTimer = new QTimer(this);
    shiftDoubleClickTimer->setSingleShot(true);
    shiftDoubleClickTimer->setInterval(DOUBLE_CLICK_INTERVAL);
    connect(shiftDoubleClickTimer, &QTimer::timeout, this, &ModeManager::onDoubleClickTimeout);

    // Setup shortcuts
    setupModeShortcuts(qobject_cast<QWidget*>(parent));

    // Apply initial mode styles
    applyModeStyles();
}

ModeManager::~ModeManager()
{
}

void ModeManager::switchMode()
{
    // Toggle mode
    currentMode = (currentMode == NormalMode) ? AlternateMode : NormalMode;

    // Apply new tab colors immediately
    applyModeStyles();

    // Update shortcuts
    updateShortcutStates();

    emit modeChanged(currentMode);
    emit modeSwitchTriggered();
}

void ModeManager::setMode(AppMode mode)
{
    if (currentMode == mode) return;

    currentMode = mode;
    applyModeStyles();
    updateShortcutStates();

    emit modeChanged(currentMode);
}

bool ModeManager::handleKeyPress(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Shift && !event->isAutoRepeat()) {
        if (!shiftPressed) {
            shiftPressed = true;
            shiftReleaseTimer->start(); // Start timeout for this press
        }
        return true; // Event handled
    }

    // If any other key is pressed, reset double-click detection
    if (event->key() != Qt::Key_Shift) {
        resetShiftDoubleClick();
    }

    return false; // Event not handled, let others process
}

bool ModeManager::handleKeyRelease(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Shift && !event->isAutoRepeat()) {
        if (shiftPressed && shiftReleaseTimer->isActive()) {
            // Valid shift press-release cycle
            shiftClickCount++;

            if (shiftClickCount == 1) {
                // First click - start double-click detection window
                shiftDoubleClickTimer->start();
            } else if (shiftClickCount == 2) {
                // Double-click detected!
                switchMode();
                resetShiftDoubleClick();
            }
        } else {
            // Invalid shift release (timeout or not properly pressed)
            resetShiftDoubleClick();
        }

        shiftPressed = false;
        shiftReleaseTimer->stop();
        return true; // Event handled
    }

    return false; // Event not handled
}

void ModeManager::onShiftTimeout()
{
    // Shift held too long - reset
    shiftPressed = false;
    resetShiftDoubleClick();
}

void ModeManager::onDoubleClickTimeout()
{
    // Double-click window expired - reset
    resetShiftDoubleClick();
}

void ModeManager::setupModeShortcuts(QWidget* parent)
{
    if (!parent) return;

    // Create normal mode shortcuts (C++11 compatible)
    normalModeShortcuts[0] = std::unique_ptr<QShortcut>(new QShortcut(QKeySequence("Ctrl+1"), parent));
    normalModeShortcuts[1] = std::unique_ptr<QShortcut>(new QShortcut(QKeySequence("Ctrl+2"), parent));
    normalModeShortcuts[2] = std::unique_ptr<QShortcut>(new QShortcut(QKeySequence("Ctrl+3"), parent));

    // Create alternate mode shortcuts with different key combinations
    alternateModeShortcuts[0] = std::unique_ptr<QShortcut>(new QShortcut(QKeySequence("Ctrl+7"), parent));
    alternateModeShortcuts[1] = std::unique_ptr<QShortcut>(new QShortcut(QKeySequence("Alt+O"), parent));
    alternateModeShortcuts[2] = std::unique_ptr<QShortcut>(new QShortcut(QKeySequence("Alt+S"), parent));



    connect(normalModeShortcuts[0].get(), &QShortcut::activated, this, [this]() {
        emit navigationToggleRequested();
    });

    connect(alternateModeShortcuts[0].get(), &QShortcut::activated, this, []() {
    });
    connect(alternateModeShortcuts[1].get(), &QShortcut::activated, this, []() {
    });
    connect(alternateModeShortcuts[2].get(), &QShortcut::activated, this, []() {
    });

    // Initially disable alternate mode shortcuts
    updateShortcutStates();
}

void ModeManager::applyModeStyles()
{
    if (!tabWidget) return;

    QString tabTextColor;
    QString tabBackgroundColor;

    if (currentMode == NormalMode) {
        tabTextColor = "#2c2c2c";
        tabBackgroundColor = "#f5f5f5";
    } else {
        tabTextColor = "#e0e0e0";
        tabBackgroundColor = "#3c3c3c";
    }

    // Apply styles to QTabBar specifically
    tabWidget->tabBar()->setStyleSheet(QString(
        "QTabBar::tab {"
        "    background-color: %2;"
        "    color: %1;"
        "    padding: 8px 12px;"
        "    margin-right: 2px;"
        "}"
        "QTabBar::tab:selected {"
        "    background-color: %2;"
        "    color: %1;"
        "}"
        "QTabBar::tab:hover {"
        "    background-color: %2;"
        "    color: %1;"
        "}")
        .arg(tabTextColor, tabBackgroundColor));

    // Preserve editor fonts after mode switch
    for (int i = 0; i < tabWidget->count(); ++i) {
        MyCodeEditor *editor = qobject_cast<MyCodeEditor*>(tabWidget->widget(i));
        if (editor) {
            editor->setFont(QFont("Consolas", 14));
        }
    }
}

void ModeManager::updateShortcutStates()
{
    // Enable/disable shortcuts based on current mode
    for (auto& shortcut : normalModeShortcuts) {
        if (shortcut) {
            shortcut->setEnabled(currentMode == NormalMode);
        }
    }

    for (auto& shortcut : alternateModeShortcuts) {
        if (shortcut) {
            shortcut->setEnabled(currentMode == AlternateMode);
        }
    }
}

void ModeManager::resetShiftDoubleClick()
{
    shiftClickCount = 0;
    shiftDoubleClickTimer->stop();
}
