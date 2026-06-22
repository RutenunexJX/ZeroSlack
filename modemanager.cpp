#include "modemanager.h"
#include <QTabWidget>
#include <QTabBar>
#include <QKeyEvent>
#include "mycodeeditor.h"
#include "mainwindow.h"

ModeManager::ModeManager(QTabWidget* tabWidget, QObject *parent)
    : QObject(parent), tabWidget(tabWidget)
{
    if (!tabWidget) {
        return;
    }

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
    Q_UNUSED(event)
    return false;
}

bool ModeManager::handleKeyRelease(QKeyEvent *event)
{
    Q_UNUSED(event)
    return false;
}

void ModeManager::setupModeShortcuts(QWidget* parent)
{
    shortcuts.setup(this, parent);
    updateShortcutStates();
}

void ModeManager::applyModeStyles()
{
    if (!tabWidget) return;

    tabWidget->tabBar()->setStyleSheet(QStringLiteral(
        "QTabBar::tab {"
        "    background-color: #f3f4f6;"
        "    color: #1f2937;"
        "    padding: 8px 12px;"
        "    margin-right: 2px;"
        "    border: 1px solid #d1d5db;"
        "    border-bottom: none;"
        "}"
        "QTabBar::tab:selected {"
        "    background-color: #2563eb;"
        "    color: #ffffff;"
        "    border-color: #1d4ed8;"
        "}"
        "QTabBar::tab:hover {"
        "    background-color: #dbeafe;"
        "    color: #111827;"
        "}"));

}

void ModeManager::updateShortcutStates()
{
    shortcuts.updateForMode(modeState);
}
