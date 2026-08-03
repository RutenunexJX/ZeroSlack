#ifndef TABMANAGER_H
#define TABMANAGER_H

#include <QObject>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QTabWidget>
#include <functional>
#include <memory>
#include "crashrecoveryservice.h"
#include "documentmodel.h"
#include "externaldocumentsynccontroller.h"
#include "editorsplitcontroller.h"
#include "mycodeeditor.h"
#include "shareddocument.h"
#include "tabdocumentqueries.h"
#include "tabfileio.h"
#include "unsaveddocumentmanager.h"
#include "workspacesessionstateservice.h"

enum class TabGroupingMode {
    None,
    Module,
    Workspace
};

QString tabGroupingModeStableId(TabGroupingMode mode);
TabGroupingMode tabGroupingModeFromStableId(const QString& stableId);

struct CrashRecoveryApplyResult :
    CrashRecoveryOperationResult {
    QString documentId;
    QString fileName;
    quint64 documentRevision = 0;
    bool openedView = false;
};

class TabManager : public QObject
{
    Q_OBJECT

public:
    using RegisteredActionRequestHandler =
        std::function<bool(const QString&, QString*)>;

    // Recovery is event-driven: first dirty revision, then each fixed
    // revision cycle, plus explicit lifecycle checkpoints.
    static constexpr quint64 kCrashRecoveryRevisionInterval = 16;

    explicit TabManager(QTabWidget* tabWidget, QObject *parent = nullptr);
    ~TabManager();

    // Tab operations
    void createNewTab();
    bool openFileInTab(const QString& fileName);
    bool saveCurrentTab();
    bool saveAsCurrentTab();
    void closeTab(int index);
    void enableSplitLayout(QWidget* host);
    bool splitCurrentView(EditorSplitDirection direction);
    bool moveCurrentViewToSplit(EditorSplitDirection direction);
    bool duplicateCurrentView();
    bool mergeCurrentSplit();
    void toggleCurrentSplitMaximized();
    void equalizeSplitSizes();
    int splitCount() const;
    EditorSplitController* editorSplitController() const;
    SharedDocument* sharedDocumentForEditor(
        MyCodeEditor* editor) const;
    ExternalDocumentSyncController*
    externalDocumentSyncController() const;
    ExternalDocumentConflictReview externalConflictReview(
        const QString& fileName);
    ExternalDocumentConflictActionResult keepLocalExternalConflict(
        const ExternalDocumentConflictReview& review);
    ExternalDocumentConflictActionResult reloadExternalConflict(
        const ExternalDocumentConflictReview& review);
    bool saveExternalConflictLocalAs(
        const ExternalDocumentConflictReview& review,
        const QString& targetFileName = QString(),
        QString* failureReason = nullptr);

    bool closeOtherTabs();
    bool closeTabsToRight();
    bool closeAllTabs();
    bool reopenClosedTab();
    bool setTabLocked(MyCodeEditor* editor, bool locked);
    bool isTabLocked(MyCodeEditor* editor) const;
    void setTabGroupingMode(TabGroupingMode mode);
    TabGroupingMode tabGroupingMode() const;
    void setRegisteredTabActionRequestHandler(
        RegisteredActionRequestHandler handler);
    bool requestTabAction(
        const QString& actionId,
        QTabWidget* group,
        int index,
        QString* failureReason = nullptr);
    bool executeRegisteredTabAction(
        const QString& actionId,
        QString* failureReason = nullptr);

    // Tab queries
    MyCodeEditor* getCurrentEditor() const;
    MyCodeEditor* getEditorAt(int index) const;
    DocumentSnapshot getCurrentDocument() const;
    // Metadata-only: text is empty and no cached editor text is copied.
    DocumentSnapshot getCurrentDocumentMetadata() const;
    DocumentSnapshot getDocumentForEditor(MyCodeEditor* editor) const;
    bool activateOpenFile(const QString& fileName);
    QString getPlainTextFromCurrentTab() const;
    QString getPlainTextFromOpenFile(const QString& fileName) const;
    QStringList getAllOpenFileNames() const;
    QStringList getOpenSystemVerilogFiles() const;
    int editorCount() const;
    QList<MyCodeEditor*> openEditors() const;
    DocumentModel* getDocumentModel() const;
    void refreshSemanticPresentations(
        const QString& changedFileName = QString());

    // Tab state management
    void updateTabTitle(MyCodeEditor* editor);
    void setWorkspaceScope(const QStringList& workspaceRoots,
                           const QString& activeWorkspaceRoot);
    bool closeTabsInWorkspace(const QString& workspaceRoot);
    bool hasUnsavedChanges() const;
    bool resolvePendingDocuments(
        QWidget* dialogParent = nullptr);
    bool prepareWorkspacePathMutation(
        const QString& sourcePath,
        bool recursive,
        QWidget* dialogParent = nullptr,
        QString* failureReason = nullptr);
    bool finalizeWorkspacePathMutation(
        const QString& sourcePath,
        bool recursive,
        QString* failureReason = nullptr);
    UnsavedDocumentManager*
    unsavedDocumentManagerForTesting() const;
    void setCrashRecoveryService(
        std::unique_ptr<CrashRecoveryService> service);
    CrashRecoveryService*
    crashRecoveryServiceForTesting() const;
    CrashRecoveryListResult listCrashRecoveryCandidates(
        const QString& workspaceRoot = QString()) const;
    CrashRecoveryReadResult compareCrashRecoveryCandidate(
        const QString& recoveryId,
        const QString& workspaceRoot = QString()) const;
    CrashRecoveryRecoverResult recoverCrashRecoveryText(
        const QString& recoveryId,
        const QString& workspaceRoot = QString()) const;
    CrashRecoveryApplyResult applyCrashRecoveryCandidate(
        const CrashRecoveryCandidate& reviewedCandidate);
    CrashRecoveryOperationResult discardCrashRecoveryCandidate(
        const QString& recoveryId,
        const QString& workspaceRoot = QString());
    void checkpointCrashRecovery();
    void clearCrashRecoveryAfterNormalClose();
    QList<WorkspaceSessionTabState> workspaceSessionTabs(
        const QString& workspaceRoot) const;
    QStringList restoreWorkspaceSessionTabs(
        const QString& workspaceRoot,
        const QList<WorkspaceSessionTabState>& tabs,
        QStringList* skippedFiles = nullptr);

signals:
    void tabCreated(MyCodeEditor* editor);
    void tabClosed(const QString& fileName);
    void fileSaved(const QString& fileName);
    void fileSaveFailed(const QString& fileName,
                        const QString& failureReason);
    void externalFileReloaded(const QString& fileName);
    void externalFileConflict(const QString& fileName);
    void externalFileUnavailable(
        const QString& fileName,
        const QString& failureReason);
    void crashRecoveryCandidatesAvailable(
        const QString& workspaceRoot,
        int candidateCount,
        int isolatedRecordCount);
    void crashRecoveryOperationFailed(
        const QString& documentId,
        const QString& failureReason);
    void activeTabChanged(MyCodeEditor* editor);
    void activeDocumentChanged(const DocumentSnapshot& snapshot);
    void tabGroupCreated(QTabWidget* group);
    void splitLayoutChanged();
    void workspaceSessionStateChanged();

private slots:
    void onTabCloseRequested(int index);
    void onCurrentTabChanged(int index);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct ClosedTabState {
        QString fileName;
        QString text;
        SharedDocumentViewState viewState;
        int groupIndex = 0;
        bool locked = false;
    };

    struct RecoveryDocumentState {
        CrashRecoveryDocumentKey key;
        quint64 snapshotRevision = 0;
        bool hasSnapshot = false;
    };

    QTabWidget* tabWidget;
    std::unique_ptr<DocumentModel> documentModel;
    std::unique_ptr<SharedDocumentRegistry> sharedDocuments;
    std::unique_ptr<ExternalDocumentSyncController>
        externalDocumentSync;
    std::unique_ptr<UnsavedDocumentManager>
        unsavedDocumentManager;
    std::unique_ptr<CrashRecoveryService>
        crashRecoveryService;
    std::unique_ptr<EditorSplitController> splitController;
    TabFileIo fileIo;
    TabDocumentQueries documentQueries;
    // Kept lexical for UI/session propagation; membership and equality use
    // EditorFileIdentity in tabmanager.cpp.
    QStringList scopedWorkspaceRoots;
    QString activeWorkspaceRoot;
    QPointer<MyCodeEditor> previousActiveEditor;
    QSet<SharedDocument*> observedDocuments;
    QSet<SharedDocument*>
        explicitExternalReloadDocuments;
    QHash<SharedDocument*, RecoveryDocumentState>
        recoveryDocumentStates;
    QSet<QString> recoveryScannedWorkspaceKeys;
    QSet<QString> lockedViewIds;
    QList<ClosedTabState> recentlyClosedTabs;
    TabGroupingMode groupingMode = TabGroupingMode::None;
    bool closingBatch = false;
    RegisteredActionRequestHandler
        registeredTabActionRequestHandler;

    QString recoveryWorkspaceForDocument(
        const SharedDocument* document) const;
    QString recoveryWorkspacePath(
        const QString& requestedWorkspace) const;
    CrashRecoveryDocumentKey recoveryKeyForDocument(
        const SharedDocument* document) const;
    bool writeRecoverySnapshot(
        SharedDocument* document,
        bool force);
    void clearRecoverySnapshot(
        SharedDocument* document,
        bool normalSave,
        bool includeUntrackedCurrent = true);
    void scanCrashRecoveryCandidates(
        const QString& workspaceRoot);
    void applyWorkspaceScope();
    bool editorVisibleInWorkspaceScope(MyCodeEditor* editor) const;
    QList<MyCodeEditor*> allEditors() const;
    QTabWidget* activeTabWidget() const;
    void registerTabGroup(QTabWidget* group);
    MyCodeEditor* createView(
        SharedDocument* document,
        QTabWidget* group,
        const SharedDocumentViewState& state = {});
    SharedDocument* acquireFileDocument(
        const QString& fileName,
        bool* loaded = nullptr);
    bool saveEditor(
        MyCodeEditor* editor,
        bool forceSaveAs,
        const QString& explicitFileName = QString());
    bool confirmCloseDocument(SharedDocument* document,
                              QString* savedFileName = nullptr);
    bool resolvePendingDocuments(
        const QList<SharedDocument*>& documents,
        QWidget* dialogParent);
    bool closeEditor(MyCodeEditor* editor,
                     bool confirmUnsaved = true,
                     bool remember = true);
    void observeDocument(SharedDocument* document);
    void updateTitlesForDocument(SharedDocument* document);
    void updateAllTabTitles();
    QString shortestDistinctTitle(
        const QString& fileName) const;
    QString tabTitleForEditor(MyCodeEditor* editor) const;
    QString tabToolTipForEditor(MyCodeEditor* editor) const;
    QString groupingKeyForEditor(MyCodeEditor* editor) const;
    void applyTabGrouping();
    void handleCurrentTabChanged(QTabWidget* group, int index);
    void handleTabAction(const QString& actionId,
                         QTabWidget* group,
                         int index);
    bool closeEditorsAtomically(
        const QList<MyCodeEditor*>& editors);
    int groupIndex(QTabWidget* group) const;
    QTabWidget* ensureGroupIndex(int index);
};

#endif // TABMANAGER_H
