#include "modemanager.h"
#include <QTabWidget>
#include <QTabBar>
#include <QKeyEvent>
#include <QFont>
#include "mycodeeditor.h"
#include "mainwindow.h"

namespace {
constexpr int kDoubleClickIntervalMs = 300;
constexpr int kShiftTimeoutMs = 1000;
}

ModeManager::ModeManager(QTabWidget* tabWidget, QObject *parent)
    : QObject(parent), tabWidget(tabWidget)
{
    if (!tabWidget) {
        return;
    }

    shiftGesture.init(this);
    setupModeShortcuts(qobject_cast<QWidget*>(parent));
    applyModeStyles();
}

ModeManager::~ModeManager()
{
}

void ModeManager::ModeState::toggle()
{
    currentMode = isNormal() ? AlternateMode : NormalMode;
}

bool ModeManager::ModeState::set(AppMode mode)
{
    if (currentMode == mode)
        return false;

    currentMode = mode;
    return true;
}

bool ModeManager::ModeState::isNormal() const
{
    return currentMode == NormalMode;
}

bool ModeManager::ModeState::isAlternate() const
{
    return currentMode == AlternateMode;
}

void ModeManager::ShiftGesture::init(ModeManager* owner)
{
    releaseTimer = new QTimer(owner);
    releaseTimer->setSingleShot(true);
    releaseTimer->setInterval(kShiftTimeoutMs);
    connect(releaseTimer, &QTimer::timeout,
            owner, &ModeManager::onShiftTimeout);

    doubleClickTimer = new QTimer(owner);
    doubleClickTimer->setSingleShot(true);
    doubleClickTimer->setInterval(kDoubleClickIntervalMs);
    connect(doubleClickTimer, &QTimer::timeout,
            owner, &ModeManager::onDoubleClickTimeout);
}

bool ModeManager::ShiftGesture::handlePress(QKeyEvent* event)
{
    if (event->key() != Qt::Key_Shift || event->isAutoRepeat())
        return false;

    if (!pressed) {
        pressed = true;
        releaseTimer->start();
    }
    return true;
}

ModeManager::ShiftReleaseAction
ModeManager::ShiftGesture::handleRelease(QKeyEvent* event)
{
    if (event->key() != Qt::Key_Shift || event->isAutoRepeat())
        return ShiftReleaseAction::NotHandled;

    ShiftReleaseAction action = ShiftReleaseAction::Handled;
    if (pressed && releaseTimer->isActive()) {
        ++clickCount;
        if (clickCount == 1) {
            doubleClickTimer->start();
        } else if (clickCount == 2) {
            action = ShiftReleaseAction::SwitchMode;
            resetDoubleClick();
        }
    } else {
        resetDoubleClick();
    }

    pressed = false;
    releaseTimer->stop();
    return action;
}

void ModeManager::ShiftGesture::handleTimeout()
{
    pressed = false;
    resetDoubleClick();
}

void ModeManager::ShiftGesture::resetDoubleClick()
{
    clickCount = 0;
    if (doubleClickTimer)
        doubleClickTimer->stop();
}

void ModeManager::ShortcutSets::setup(ModeManager* owner, QWidget* parent)
{
    if (!parent)
        return;

    normalModeShortcuts[0] = std::make_unique<QShortcut>(
        QKeySequence("Ctrl+1"), parent);
    normalModeShortcuts[1] = std::make_unique<QShortcut>(
        QKeySequence("Ctrl+2"), parent);
    normalModeShortcuts[2] = std::make_unique<QShortcut>(
        QKeySequence("Ctrl+3"), parent);
    alternateModeShortcuts[0] = std::make_unique<QShortcut>(
        QKeySequence("Ctrl+7"), parent);
    alternateModeShortcuts[1] = std::make_unique<QShortcut>(
        QKeySequence("Alt+O"), parent);
    alternateModeShortcuts[2] = std::make_unique<QShortcut>(
        QKeySequence("Alt+S"), parent);

    connect(normalModeShortcuts[0].get(), &QShortcut::activated,
            owner, [owner]() {
                emit owner->navigationToggleRequested();
            });

    connect(alternateModeShortcuts[0].get(), &QShortcut::activated, owner, []() {
    });
    connect(alternateModeShortcuts[1].get(), &QShortcut::activated, owner, []() {
    });
    connect(alternateModeShortcuts[2].get(), &QShortcut::activated, owner, []() {
    });
}

void ModeManager::ShortcutSets::updateForMode(const ModeState& modeState)
{
    for (auto& shortcut : normalModeShortcuts) {
        if (shortcut)
            shortcut->setEnabled(modeState.isNormal());
    }

    for (auto& shortcut : alternateModeShortcuts) {
        if (shortcut)
            shortcut->setEnabled(modeState.isAlternate());
    }
}

ModeManager::AppMode ModeManager::getCurrentMode() const
{
    return modeState.currentMode;
}

void ModeManager::switchMode()
{
    modeState.toggle();
    applyModeStyles();
    updateShortcutStates();

    emit modeChanged(modeState.currentMode);
    emit modeSwitchTriggered();
}

void ModeManager::setMode(AppMode mode)
{
    if (!modeState.set(mode))
        return;
    applyModeStyles();
    updateShortcutStates();

    emit modeChanged(modeState.currentMode);
}

bool ModeManager::handleKeyPress(QKeyEvent *event)
{
    if (shiftGesture.handlePress(event))
        return true;

    if (event->key() != Qt::Key_Shift) {
        resetShiftDoubleClick();
    }

    return false; // Event not handled, let others process
}

bool ModeManager::handleKeyRelease(QKeyEvent *event)
{
    const ShiftReleaseAction action = shiftGesture.handleRelease(event);
    if (action == ShiftReleaseAction::NotHandled)
        return false;
    if (action == ShiftReleaseAction::SwitchMode)
        switchMode();

        return true; // Event handled
}

void ModeManager::onShiftTimeout()
{
    shiftGesture.handleTimeout();
}

void ModeManager::onDoubleClickTimeout()
{
    resetShiftDoubleClick();
}

void ModeManager::setupModeShortcuts(QWidget* parent)
{
    shortcuts.setup(this, parent);
    updateShortcutStates();
}

void ModeManager::applyModeStyles()
{
    if (!tabWidget) return;

    QString tabTextColor;
    QString tabBackgroundColor;

    if (modeState.isNormal()) {
        tabTextColor = "#2c2c2c";
        tabBackgroundColor = "#f5f5f5";
    } else {
        tabTextColor = "#e0e0e0";
        tabBackgroundColor = "#3c3c3c";
    }

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

    for (int i = 0; i < tabWidget->count(); ++i) {
        MyCodeEditor *editor = qobject_cast<MyCodeEditor*>(tabWidget->widget(i));
        if (editor) {
            editor->setFont(QFont("Consolas", 14));
        }
    }
}

void ModeManager::updateShortcutStates()
{
    shortcuts.updateForMode(modeState);
}

void ModeManager::resetShiftDoubleClick()
{
    shiftGesture.resetDoubleClick();
}
