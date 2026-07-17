#ifndef EDITORCOORDINATOR_H
#define EDITORCOORDINATOR_H

#include <QObject>
#include <QHash>
#include <QString>
#include <QStringList>
#include <functional>

#include "includeheaderworkflowtypes.h"
#include "saferenameservice.h"

class FileCommandCoordinator;
class EditorAppearanceSettings;
class FormatterSettings;
class EditorSemanticContextService;
enum class FormatterProfile;
struct EditorSemanticContext;
struct EditorSourceNavigationTarget;
enum class SourceSymbolAction;
class MyCodeEditor;
class NavigationCommandCoordinator;
class QMenu;
class SemanticPanelRefreshCoordinator;
class TabManager;
class WorkspaceManager;

class EditorCoordinator : public QObject
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
    void setFormatterSettings(FormatterSettings* settings);
    void setStatusMessageHandler(
        std::function<void(const QString&, int)> handler);
    void setFoldShelfRequestedHandler(std::function<void()> handler);
    void setFoldShelfItemConsumedHandler(std::function<void(const QString&)> handler);

    void connectSignals();
    void attachEditor(MyCodeEditor* editor);

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
        void navigateBack() const;
        void navigateForward() const;
        void showReferencesForSymbol(const QString& symbolName,
                                     const QString& fileName,
                                     const QString& moduleName) const;
        void showRelationshipsForSymbol(const QString& symbolName,
                                        const QString& fileName,
                                        const QString& moduleName) const;
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
    void applyFormatterSettings(MyCodeEditor* editor) const;
    void applyFormatterSettingsToOpenEditors() const;
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
        int column) const;
    void handleSourceNavigationRequested(
        MyCodeEditor* editor,
        const EditorSourceNavigationTarget& target,
        const EditorSemanticContext& context) const;
    void handleSourceSymbolActionRequested(
        SourceSymbolAction action,
        const EditorSemanticContext& context) const;
    void handleSafeRenameRequested(
        MyCodeEditor* editor,
        const QString& symbolName,
        const EditorSemanticContext& context,
        bool* handled);
    void handleSourceSymbolContextMenuRequested(
        QMenu* menu,
        const EditorSemanticContext& context) const;
    void handleActiveEditorChanged(MyCodeEditor* editor);
    QHash<QString, QString> openFileContents() const;
    SafeRenamePlanQuery safeRenameQuery(
        const QString& symbolName,
        const QString& newName,
        const EditorSemanticContext& context,
        bool forceConflicts = false) const;
    bool applySafeRenamePlan(MyCodeEditor* originEditor,
                             const SafeRenamePlan& plan) const;
    bool createDefinitionAndRenameCurrentFile(
        MyCodeEditor* editor,
        const QString& symbolName,
        const QString& newName,
        const EditorSemanticContext& context) const;

    TabManager* tabManager = nullptr;
    EditorAppearanceSettings* appearanceSettings = nullptr;
    FormatterSettings* formatterSettings = nullptr;
    QMetaObject::Connection appearanceSettingsConnection;
    QMetaObject::Connection formatterSettingsConnection;
    QMetaObject::Connection formatterFormatOnSaveConnection;
    WorkflowDependencies dependencies;
    SemanticRuntime semanticRuntime;
    std::function<void(const QString&, int)> statusMessageHandler;
    std::function<void()> foldShelfRequestedHandler;
    std::function<void(const QString&)> foldShelfItemConsumedHandler;
    bool signalsConnected = false;
    mutable bool applyingFormatterSettings = false;
};

#endif // EDITORCOORDINATOR_H
