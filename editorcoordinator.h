#ifndef EDITORCOORDINATOR_H
#define EDITORCOORDINATOR_H

#include "zeroslackexport.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <functional>
#include <memory>

#include "editoractioncontextservice.h"
#include "annotationlayer.h"
#include "editormodecontroller.h"
#include "includeheaderworkflowtypes.h"

class FileCommandCoordinator;
class EditorAppearanceSettings;
class EditorSemanticContextService;
struct EditorSemanticContext;
struct EditorSourceNavigationTarget;
struct HierarchyInstanceContext;
enum class SourceSymbolAction;
class MyCodeEditor;
class NavigationCommandCoordinator;
class QMenu;
class SemanticPanelRefreshCoordinator;
class TabManager;
class WorkspaceManager;
struct ActionDescriptor;
struct ActionExecutionResult;
struct ActionInvocation;

class ZEROSLACK_API EditorCoordinator : public QObject
{
    Q_OBJECT

public:
    explicit EditorCoordinator(TabManager* tabManager,
                               QObject* parent = nullptr);

    void setWorkflowDependencies(
        WorkspaceManager* workspaceManager,
        FileCommandCoordinator* fileCommandCoordinator,
        NavigationCommandCoordinator* navigationCommandCoordinator,
        SemanticPanelRefreshCoordinator* semanticPanelRefresh);
    void setAppearanceSettings(EditorAppearanceSettings* settings);
    void setAnnotationDisplayOptions(
        const EditorAnnotationDisplayOptions& options);
    void setStatusMessageHandler(
        std::function<void(const QString&, int)> handler);
    void setModeStateHandler(
        std::function<void(const EditorModeSnapshot&)> handler);
    void setActionContextService(
        EditorActionContextService* service);
    void setActionContextQueryProvider(
        std::function<EditorActionContextQuery(
            const EditorSemanticContext&)> provider);
    void setRegisteredActionRequestHandler(
        std::function<void(const QString&,
                           const QVariantMap&)> handler);
    void setFoldShelfItemConsumedHandler(std::function<void(const QString&)> handler);

    void connectSignals();
    void attachEditor(MyCodeEditor* editor);
    void populateSourceSymbolContextMenuForTest(
        QMenu* menu,
        const EditorSemanticContext& context) const;
    ActionExecutionResult executeRegisteredSourceAction(
        const ActionDescriptor& descriptor,
        const ActionInvocation& invocation) const;
    ActionExecutionResult executeRegisteredExposeSignalAction(
        const ActionDescriptor& descriptor,
        const ActionInvocation& invocation) const;

private:
    struct WorkflowDependencies {
        WorkspaceManager* workspaceManager = nullptr;
        FileCommandCoordinator* fileCommandCoordinator = nullptr;
        NavigationCommandCoordinator* navigationCommandCoordinator = nullptr;
        SemanticPanelRefreshCoordinator* semanticPanelRefresh = nullptr;

        void set(WorkspaceManager* workspaceManager,
                 FileCommandCoordinator* fileCommandCoordinator,
                 NavigationCommandCoordinator* navigationCommandCoordinator,
                 SemanticPanelRefreshCoordinator* semanticPanelRefresh);
        QString resolveIncludePath(const QString& includePath,
                                   const QString& currentFile) const;
        QStringList includeFileCompletionCandidates(
            const QString& currentFile) const;
        void navigateEditorToLine(MyCodeEditor* editor,
                                  int line,
                                  int column) const;
        void navigateToFileAndLine(const QString& fileName,
                                   int line,
                                   int column) const;
        void navigateToFileAndLineWithContext(
            const QString& fileName,
            int line,
            int column,
            const HierarchyInstanceContext& instanceContext) const;
        void navigateBack() const;
        void navigateForward() const;
        void showSignalKernelGraphForSymbol(const QString& symbolName,
                                            const QString& fileName,
                                            const QString& moduleName,
                                            const QString& signalAccessPath = {}) const;
        void showSignalUsageHotspotForSymbol(const QString& symbolName,
                                             const QString& fileName,
                                             const QString& moduleName,
                                             const QString& signalAccessPath = {}) const;
        void showStateTransitionGraphForSymbol(const QString& symbolName,
                                               const QString& fileName,
                                               const QString& moduleName) const;
        void showModuleBlockDiagramForSymbol(const QString& symbolName,
                                             const QString& fileName,
                                             const QString& moduleName) const;
        void handleActiveEditorChanged(MyCodeEditor* editor) const;
        bool canNavigate() const;
        bool hasSemanticPanelRefresh() const;
    };

    struct SemanticRuntime {
        EditorSemanticContextService* service = nullptr;

        void init();
        EditorSemanticContextService* contextService() const;
    };

    EditorSemanticContextService* contextService() const;
    void applyAppearance(MyCodeEditor* editor) const;
    void applyAppearanceToOpenEditors() const;
    void applyAnnotationDisplayOptions(
        MyCodeEditor* editor) const;
    void applyAnnotationDisplayOptionsToOpenEditors() const;
    void handleIncludeOpenRequested(MyCodeEditor* editor,
                                    const QString& includePath,
                                    const QString& currentFile) const;
    IncludeNewHeaderResult createIncludeNewHeader(
        const IncludeNewHeaderRequest& request) const;
    void handleDefinitionNavigationRequested(
        MyCodeEditor* editor,
        const QString& symbolName,
        const EditorSemanticContext& context) const;
    void handleDefinitionPreviewNavigationRequested(
        const QString& fileName,
        int line,
        int column,
        const HierarchyInstanceContext& instanceContext) const;
    void handleSourceNavigationRequested(
        MyCodeEditor* editor,
        const EditorSourceNavigationTarget& target,
        const EditorSemanticContext& context) const;
    void handleSourceSymbolActionRequested(
        SourceSymbolAction action,
        const EditorSemanticContext& context) const;
    bool executeSourceSymbolActionRequested(
        SourceSymbolAction action,
        const EditorSemanticContext& context,
        QString* failureReason) const;
    void handleSourceSymbolContextMenuRequested(
        QMenu* menu,
        MyCodeEditor* editor,
        const EditorSemanticContext& context) const;
    EditorActionContext actionContextFor(
        const EditorSemanticContext& context) const;
    void handleExposeSignalToTopRequested(
        const EditorSemanticContext& context) const;
    void handleActiveEditorChanged(MyCodeEditor* editor);

    TabManager* tabManager = nullptr;
    EditorAppearanceSettings* appearanceSettings = nullptr;
    EditorAnnotationDisplayOptions annotationDisplayOptions;
    QMetaObject::Connection appearanceSettingsConnection;
    WorkflowDependencies dependencies;
    SemanticRuntime semanticRuntime;
    std::unique_ptr<EditorActionContextService>
        ownedActionContextService;
    EditorActionContextService* actionContextService = nullptr;
    std::function<void(const QString&, int)> statusMessageHandler;
    std::function<void(const EditorModeSnapshot&)> modeStateHandler;
    std::function<EditorActionContextQuery(
        const EditorSemanticContext&)> actionContextQueryProvider;
    std::function<void(const QString&,
                       const QVariantMap&)>
        registeredActionRequestHandler;
    std::function<void(const QString&)> foldShelfItemConsumedHandler;
    bool signalsConnected = false;
};

#endif // EDITORCOORDINATOR_H
