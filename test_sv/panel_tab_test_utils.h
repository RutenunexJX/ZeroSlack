#pragma once

#include "editorsplitcontroller.h"
#include "panellayoutcontroller.h"
#include <QAccessible>
#include <QMenu>
#include <QTabBar>
#include <QTabWidget>
#include <QTest>
#include <QToolButton>

namespace PanelTabTest {
inline QWidget* page(PanelLayoutController* controller, const QString& id)
{
    auto* registry = controller ? controller->tabHostController() : nullptr;
    return registry ? registry->pageById(QStringLiteral("tool:") + id) : nullptr;
}
inline QTabWidget* group(PanelLayoutController* controller, const QString& id)
{
    auto* content = page(controller, id);
    return content ? controller->tabHostController()->groupForPage(content) : nullptr;
}
inline bool visible(PanelLayoutController* controller, const QString& id)
{
    auto* tabs = group(controller, id);
    return tabs && tabs->tabBar()->isVisible() && tabs->isTabVisible(tabs->indexOf(page(controller, id)));
}
inline QString accessibleName(PanelLayoutController* controller, const QString& id)
{
    auto* tabs = group(controller, id);
    if (!tabs) return {};
    auto* accessible = QAccessible::queryAccessibleInterface(tabs->tabBar());
    auto* tab = accessible ? accessible->child(tabs->indexOf(page(controller, id))) : nullptr;
    return tab ? tab->text(QAccessible::Name) : QString();
}
inline bool reopen(PanelLayoutController* controller, const QString& id)
{
    auto* strip=controller ? controller->tabStrip() : nullptr;
    auto* button=strip ? strip->findChild<QToolButton*>(QStringLiteral("bottomRestorePanels")) : nullptr;
    if (!button || !button->isVisible() || !button->isEnabled() || !button->menu()) return false;
    auto* menu=button->menu();
    menu->popup(button->mapToGlobal(QPoint(0,button->height())));
    QCoreApplication::processEvents();
    for (auto* action:menu->actions()) {
        if (action->data().toString()!=id || !action->isEnabled()) continue;
        action->trigger(); menu->close(); return true;
    }
    menu->close(); return false;
}
inline bool click(PanelLayoutController* controller, const QString& id)
{
    auto* tabs = group(controller, id);
    if (!tabs) return false;
    auto* bar = tabs->tabBar();
    const int index = tabs->indexOf(page(controller, id));
    if (index < 0 || !bar->isVisible() || !bar->isTabVisible(index)) return false;
    for (int attempt = 0; attempt <= bar->count(); ++attempt) {
        const auto rect = bar->tabRect(index);
        const auto position = rect.center();
        bool exposed = bar->rect().contains(position);
        for (auto* button : bar->findChildren<QToolButton*>(QString(), Qt::FindDirectChildrenOnly))
            exposed &= !button->isVisible() || !button->geometry().contains(position);
        if (exposed) {
            QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, position);
            return true;
        }
        auto* scroll = bar->findChild<QToolButton*>(rect.left() < 0
            ? QStringLiteral("ScrollLeftButton") : QStringLiteral("ScrollRightButton"));
        if (!scroll || !scroll->isVisible() || !scroll->isEnabled()) return false;
        QTest::mouseClick(scroll, Qt::LeftButton);
        QCoreApplication::processEvents();
    }
    return false;
}
}
