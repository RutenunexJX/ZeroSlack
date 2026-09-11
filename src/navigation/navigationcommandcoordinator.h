#ifndef NAVIGATIONCOMMANDCOORDINATOR_H
#define NAVIGATIONCOMMANDCOORDINATOR_H

#include "zeroslackexport.h"

#include <QObject>
#include <QString>
#include <QVector>

#include "symbolpresentationservice.h"

class NavigationManager;
class MyCodeEditor;
class TabManager;
class WorkspaceManager;

class ZEROSLACK_API NavigationCommandCoordinator : public QObject
{
    Q_OBJECT

public:
    explicit NavigationCommandCoordinator(TabManager* tabManager,
                                          NavigationManager* navigationManager,
                                          WorkspaceManager* workspaceManager = nullptr,
                                          QObject* parent = nullptr);

    void connectSignals();
    void navigateToFileAndLine(const QString& filePath,
                               int lineNumber = -1,
                               int columnNumber = -1);
    void navigateToFileAndLineWithContext(
        const QString& filePath,
        int lineNumber,
        int columnNumber,
        const HierarchyInstanceContext& instanceContext);
    bool navigateToFileAndLineAndFlash(const QString& filePath,
                                       int lineNumber,
                                       int columnNumber = -1);
    void revealFileAndFlashLine(const QString& filePath,
                                int lineNumber);
    void navigateEditorToLine(MyCodeEditor* editor,
                              int lineNumber,
                              int columnNumber = -1);
    void navigateBack();
    void navigateForward();

private:
    struct NavigationLocation {
        QString filePath;
        QString workspacePath;
        HierarchyInstanceContext instanceContext;
        int lineNumber = -1;
        int columnNumber = -1;

        bool isValid() const;
        bool operator==(const NavigationLocation& other) const;
    };

    struct NavigationTargets {
        TabManager* tabManager = nullptr;
        NavigationManager* navigationManager = nullptr;
        WorkspaceManager* workspaceManager = nullptr;

        void set(TabManager* tabManager,
                 NavigationManager* navigationManager,
                 WorkspaceManager* workspaceManager);
        bool hasNavigationManager() const;
        NavigationManager* navigationManagerObject() const;
        bool activateOrOpenFile(const QString& filePath) const;
        MyCodeEditor* currentEditor() const;
        NavigationLocation currentLocation() const;
        QString currentWorkspacePath() const;
    };

    struct LineNavigationResolver {
        bool applyToEditor(MyCodeEditor* editor,
                           int lineNumber,
                           int columnNumber) const;
    };

    NavigationTargets targets;
    LineNavigationResolver lineResolver;
    QVector<NavigationLocation> backStack;
    QVector<NavigationLocation> forwardStack;
    bool replayingHistory = false;
    bool signalsConnected = false;

    void recordLocationBeforeNavigation(
        const NavigationLocation& current,
        const NavigationLocation& destination);
    void recordCurrentLocationBeforeNavigation(
        const NavigationLocation& destination);
    bool applyLocation(const NavigationLocation& location);
    void pruneHistoryForCurrentWorkspace();
    HierarchyInstanceContext normalizedInstanceContext(
        const HierarchyInstanceContext& context) const;
};

#endif // NAVIGATIONCOMMANDCOORDINATOR_H
