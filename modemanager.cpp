#include "modemanager.h"
#include "insightvisualstyle.h"
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
    connect(normalModeShortcuts[0].get(), &QShortcut::activated,
            owner, [owner]() {
                emit owner->navigationToggleRequested();
            });
}

void ModeManager::ShortcutSets::updateForMode(const ModeState& modeState)
{
    for (auto& shortcut : normalModeShortcuts) {
        if (shortcut)
            shortcut->setEnabled(modeState.isNormal());
    }
}

ModeManager::AppMode ModeManager::getCurrentMode() const
{
    return modeState.currentMode;
}

void ModeManager::switchMode()
{
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

    QTabBar* bar = tabWidget->tabBar();
    if (!bar)
        return;
    if (bar->objectName().isEmpty())
        bar->setObjectName(QStringLiteral("mainEditorTabBar"));
    bar->setStyleSheet(InsightVisualStyle::tabBarStyleSheet(
        bar->objectName()));

}

void ModeManager::updateShortcutStates()
{
    shortcuts.updateForMode(modeState);
}
