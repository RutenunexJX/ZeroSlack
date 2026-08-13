#ifndef RTLACTIONCOORDINATOR_H
#define RTLACTIONCOORDINATOR_H

#include "actionregistry.h"
#include "editoractioncontextservice.h"
#include "zeroslackexport.h"

#include <QObject>
#include <QString>

#include <functional>

class NotificationCenter;
class PanelLayoutController;
class QAction;
class QMenu;
class SemanticDockCoordinator;
class TabManager;
class QWidget;
class WorkspaceManager;

struct RtlActionCoordinatorCallbacks {
    std::function<EditorActionContext(
        const EditorSemanticContext& context)>
        resolveContext;
    std::function<void(const QString& panelId)>
        showPanel;
};

class ZEROSLACK_API RtlActionCoordinator final : public QObject
{
    Q_OBJECT

public:
    explicit RtlActionCoordinator(
        TabManager* tabManager,
        WorkspaceManager* workspaceManager,
        SemanticDockCoordinator* semanticDocks,
        PanelLayoutController* panelLayoutController,
        QWidget* dialogParent,
        RtlActionCoordinatorCallbacks callbacks,
        NotificationCenter* notificationCenter = nullptr,
        QObject* parent = nullptr);

    static bool handlesRoute(const QString& executionRoute);
    ActionExecutionResult execute(
        const ActionDescriptor& descriptor,
        const ActionInvocation& invocation);
    void connectActionAvailability(
        QMenu* menu,
        QAction* renameAction,
        QAction* connectionTransformAction,
        QAction* instancePairAction,
        QAction* multiSignalAction);

private:
    TabManager* tabManager = nullptr;
    WorkspaceManager* workspaceManager = nullptr;
    SemanticDockCoordinator* semanticDocks = nullptr;
    PanelLayoutController* panelLayoutController = nullptr;
    QWidget* dialogParent = nullptr;
    NotificationCenter* notificationCenter = nullptr;
    RtlActionCoordinatorCallbacks callbacks;

    void connectPanelSignals();
    EditorActionContext resolveContext(
        const EditorSemanticContext& context) const;
    void showPanel(const QString& panelId) const;

    ActionExecutionResult executeRtlRenameAction(
        const ActionInvocation& invocation);
    ActionExecutionResult executeRtlConnectionTransformAction(
        const ActionInvocation& invocation);
    ActionExecutionResult executeInstancePairConnectionAction(
        const ActionInvocation& invocation);
    ActionExecutionResult executeMultiSignalPropagationAction(
        const ActionInvocation& invocation);
};

#endif // RTLACTIONCOORDINATOR_H
