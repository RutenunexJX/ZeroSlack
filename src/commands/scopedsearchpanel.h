#ifndef SCOPEDSEARCHPANEL_H
#define SCOPEDSEARCHPANEL_H

#include "zeroslackexport.h"

#include "actionregistry.h"
#include "editorlocation.h"
#include "searchservice.h"

#include <QList>
#include <QMetaType>
#include <QPointer>
#include <QString>
#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QDockWidget;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPoint;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class ScopedReplaceWorkflow;
struct ScopedReplaceWorkflowResult;

struct ScopedSearchPanelContext {
    QList<SearchDocumentSnapshot> documents;
    QList<SearchDocumentSnapshot> workspaceDocuments;
    QString activeFileName;
    int cursorChar = -1;
    bool workspaceDocumentsSpecified = false;
};

Q_DECLARE_METATYPE(ScopedSearchScope)
Q_DECLARE_METATYPE(ScopedSearchResponse)
Q_DECLARE_METATYPE(ReplacePreviewPlan)

class ZEROSLACK_API ScopedSearchPanel : public QWidget
{
    Q_OBJECT

public:
    using ContextProvider =
        std::function<ScopedSearchPanelContext()>;

    explicit ScopedSearchPanel(
        const SearchService* service = nullptr,
        QWidget* parent = nullptr);

    void setSearchService(const SearchService* service);
    void setContextProvider(ContextProvider provider);
    void setSearchContext(
        const ScopedSearchPanelContext& context);
    void setReplaceWorkflow(
        ScopedReplaceWorkflow* workflow);

    void setQueryText(const QString& text);
    QString queryText() const;
    void setScope(ScopedSearchScope scope);
    ScopedSearchScope scope() const;
    void setReplacementText(const QString& text);
    QString replacementText() const;

    const ScopedSearchResponse& response() const;
    const ReplacePreviewPlan& replacePreview() const;
    int displayedResultCount() const;
    int displayedReplaceMatchCount() const;

    QLineEdit* queryEditor() const { return queryEdit; }
    QComboBox* scopeSelector() const { return scopeCombo; }
    QTreeWidget* resultsView() const { return resultsTree; }
    QLineEdit* replacementEditor() const {
        return replacementEdit;
    }
    QTreeWidget* replaceChecklist() const {
        return replaceTree;
    }
    QPushButton* previewButton() const {
        return buildPreviewButton;
    }
    QCheckBox* dryRunControl() const {
        return dryRunCheck;
    }
    QPlainTextEdit* diffView() const {
        return replaceDiffView;
    }
    QPushButton* applyButton() const {
        return applyReplaceButton;
    }
    QPushButton* cancelButton() const {
        return cancelReplaceButton;
    }
    QPushButton* undoButton() const {
        return undoReplaceButton;
    }
    QLabel* searchStatus() const { return searchStatusLabel; }
    QLabel* replaceStatus() const { return replaceStatusLabel; }
    bool requestTemporaryEditorOpenForItem(
        QTreeWidgetItem* item);

public slots:
    void refresh();
    ReplacePreviewPlan buildReplacePreview();
    void confirmReplace();
    void cancelReplace();
    void undoReplace();

signals:
    void navigationRequested(
        const QString& fileName,
        int line,
        int column);
    void temporaryEditorOpenRequested(
        const EditorLocation& location);
    void searchCompleted(
        const ScopedSearchResponse& response);
    void replacePreviewReady(
        const ReplacePreviewPlan& preview);

private:
    void buildUi();
    ScopedSearchPanelContext currentContext() const;
    QList<SearchDocumentSnapshot> documentsForScope(
        const ScopedSearchPanelContext& context) const;
    ScopedSearchQuery currentQuery(
        const ScopedSearchPanelContext& context) const;
    void renderResponse();
    void renderReplaceChecklist();
    void activateResultItem(QTreeWidgetItem* item);
    void showResultContextMenu(const QPoint& position);
    void updateReplaceSelection(QTreeWidgetItem* item);
    void invalidateReplacePreview(
        const QString& message = QString());
    ReplacePreviewRequest replaceRequestFromChecklist() const;
    void updateWorkflowUi(
        const ScopedReplaceWorkflowResult& result);
    void updateReplaceButtons();

    const SearchService* searchService = nullptr;
    ContextProvider contextProvider;
    ScopedSearchPanelContext fallbackContext;
    ScopedSearchPanelContext searchedContext;
    ScopedSearchResponse currentResponse;
    ReplacePreviewPlan currentReplacePreview;
    QPointer<ScopedReplaceWorkflow> replaceWorkflow;

    QLineEdit* queryEdit = nullptr;
    QComboBox* scopeCombo = nullptr;
    QCheckBox* caseSensitiveCheck = nullptr;
    QCheckBox* wholeWordCheck = nullptr;
    QCheckBox* semanticCheck = nullptr;
    QPushButton* searchButton = nullptr;
    QLabel* searchStatusLabel = nullptr;
    QTreeWidget* resultsTree = nullptr;
    QLineEdit* replacementEdit = nullptr;
    QTreeWidget* replaceTree = nullptr;
    QPushButton* buildPreviewButton = nullptr;
    QCheckBox* dryRunCheck = nullptr;
    QPlainTextEdit* replaceDiffView = nullptr;
    QPushButton* applyReplaceButton = nullptr;
    QPushButton* cancelReplaceButton = nullptr;
    QPushButton* undoReplaceButton = nullptr;
    QLabel* replaceStatusLabel = nullptr;
    bool updatingReplaceChecklist = false;
    int renderedResultCount = 0;
    int renderedReplaceMatchCount = 0;
};

// Owns one dock and one page for its complete lifetime. It deliberately has
// no open/close API: the registered PanelLayoutController remains the sole
// owner of visibility, tab selection, and restored height.
class ZEROSLACK_API ScopedSearchPanelCoordinator : public QObject
{
    Q_OBJECT

public:
    using NavigationHandler =
        std::function<void(const QString&, int, int)>;

    explicit ScopedSearchPanelCoordinator(
        QWidget* dockParent,
        const SearchService* service = nullptr,
        QObject* parent = nullptr);

    static QString panelId();

    QDockWidget* dock() const;
    ScopedSearchPanel* panel() const;

    void setContextProvider(
        ScopedSearchPanel::ContextProvider provider);
    void setSearchContext(
        const ScopedSearchPanelContext& context);
    void setNavigationHandler(NavigationHandler handler);
    void setRegisteredActionRequestHandler(
        RegisteredActionRequestHandler handler);
    void setReplaceWorkflow(
        ScopedReplaceWorkflow* workflow);
    ActionExecutionResult requestTemporaryEditorOpen(
        const EditorLocation& location);

public slots:
    void refresh();
    ReplacePreviewPlan buildReplacePreview();

signals:
    void navigationRequested(
        const QString& fileName,
        int line,
        int column);
    void temporaryEditorOpenRequested(
        const EditorLocation& location);
    void temporaryEditorOpenFinished(
        const EditorLocation& location,
        bool succeeded,
        const QString& failureReason);
    void replacePreviewReady(
        const ReplacePreviewPlan& preview);

private:
    QPointer<QDockWidget> searchDock;
    QPointer<ScopedSearchPanel> searchPanel;
    NavigationHandler navigationHandler;
    RegisteredActionRequestHandler
        registeredActionRequestHandler;
};

#endif // SCOPEDSEARCHPANEL_H
