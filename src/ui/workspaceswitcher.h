#pragma once

#include "zeroslackexport.h"
#include <QObject>
#include <QList>
#include <QPointer>
#include <functional>

class QTabBar;
class QWidget;
class WorkspaceManager;
class WorkspaceSessionCoordinator;
class TabManager;

class ZEROSLACK_API WorkspaceSwitcher final : public QObject {
public:
    WorkspaceSwitcher(WorkspaceManager* workspaces, TabManager* tabs,
                      WorkspaceSessionCoordinator* sessions,
                      std::function<void()> openWorkspace, QWidget* parent);
    QWidget* createStrip(QWidget* parent);
private:
    bool eventFilter(QObject* object, QEvent* event) override;
    void refresh();
    void showContextMenu(QTabBar* bar, const QPoint& point);
    void requestWorkspace(const QString& path, bool close);
    QPointer<WorkspaceManager> workspaces;
    QPointer<TabManager> tabs;
    QPointer<WorkspaceSessionCoordinator> sessions;
    std::function<void()> openWorkspace;
    QList<QPointer<QTabBar>> bars;
};
