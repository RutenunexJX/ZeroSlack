#include "scopedsearchpanel.h"

#include "editorfileidentity.h"
#include "mainwindow.h"
#include "mycodeeditor.h"
#include "panellayoutcontroller.h"
#include "shareddocument.h"
#include "tabmanager.h"
#include "tsdocument.h"
#include "workspacemanager.h"

#include <rtledit/edit_plan.h>

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QFile>
#include <QLineEdit>
#include <QMainWindow>
#include <QMetaObject>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <iostream>

namespace {
int checks = 0;
int failures = 0;

void check(bool condition, const char* message)
{
    ++checks;
    if (condition)
        return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

SemanticSymbolRecord signalRecord(
    const QString& fileName,
    const QString& text,
    int position,
    const QString& name)
{
    SemanticSymbolRecord record;
    record.name = name;
    record.declarationKind =
        SymbolTaxonomy::DeclarationKind::Signal;
    record.usageRole =
        SymbolTaxonomy::SymbolUsageRole::Declaration;
    record.owner.kind =
        SymbolTaxonomy::SymbolOwnerScope::Module;
    record.owner.name = QStringLiteral("first");
    record.location.fileName = fileName;
    record.location.position = position;
    record.location.length = name.size();
    record.location.startLine =
        text.left(position).count(QLatin1Char('\n')) + 1;
    const int lineStart =
        text.lastIndexOf(QLatin1Char('\n'), position - 1) + 1;
    record.location.startColumn =
        position - lineStart + 1;
    record.location.endLine = record.location.startLine;
    record.location.endColumn =
        record.location.startColumn + name.size();
    record.stableKey.fileName = fileName;
    record.stableKey.symbolName = name;
    record.stableKey.declarationKind =
        record.declarationKind;
    record.stableKey.ownerScope = record.owner.name;
    record.stableKey.sourcePosition = position;
    record.stableKey.sourceLength = name.size();
    return record;
}

int childCount(const QTreeWidget* tree)
{
    int result = 0;
    if (!tree)
        return result;
    for (int index = 0;
         index < tree->topLevelItemCount();
         ++index) {
        result +=
            tree->topLevelItem(index)->childCount();
    }
    return result;
}

QTreeWidgetItem* firstMatch(QTreeWidget* tree)
{
    if (!tree)
        return nullptr;
    for (int index = 0;
         index < tree->topLevelItemCount();
         ++index) {
        QTreeWidgetItem* fileItem =
            tree->topLevelItem(index);
        if (fileItem->childCount() > 0)
            return fileItem->child(0);
    }
    return nullptr;
}

QTreeWidgetItem* fileItemFor(
    QTreeWidget* tree,
    const QString& fileName)
{
    if (!tree)
        return nullptr;
    for (int index = 0;
         index < tree->topLevelItemCount();
         ++index) {
        QTreeWidgetItem* item =
            tree->topLevelItem(index);
        if (EditorFileIdentity::same(
                item->text(0), fileName)) {
            return item;
        }
    }
    return nullptr;
}

bool writeUtf8File(const QString& fileName,
                   const QString& text)
{
    QFile file(fileName);
    return file.open(QIODevice::WriteOnly
                     | QIODevice::Truncate)
        && file.write(text.toUtf8())
               == text.toUtf8().size();
}
} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QTemporaryDir workspace;
    check(workspace.isValid(),
          "temporary workspace is available");
    if (!workspace.isValid())
        return 1;

    const QString firstFile =
        workspace.filePath(QStringLiteral("first.sv"));
    const QString secondFile =
        workspace.filePath(QStringLiteral("second.sv"));
    const QString thirdFile =
        workspace.filePath(QStringLiteral("third.sv"));
    const QString firstText =
        QStringLiteral(
            "module first;\n"
            "  logic target;\n"
            "  always_comb begin\n"
            "    target = target + 1;\n"
            "  end\n"
            "endmodule\n");
    const QString secondText =
        QStringLiteral(
            "module second;\n"
            "  // target is mirrored\n"
            "  logic target;\n"
            "endmodule\n");
    const QString diskFirstText =
        QStringLiteral(
            "module first;\n"
            "  logic disk_stale;\n"
            "endmodule\n");
    const QString thirdText =
        QStringLiteral(
            "module third;\n"
            "  logic disk_only;\n"
            "endmodule\n");
    check(writeUtf8File(firstFile, diskFirstText)
              && writeUtf8File(secondFile, secondText)
              && writeUtf8File(thirdFile, thirdText),
          "workspace fixtures are written");

    TSDocument firstSyntax;
    firstSyntax.setText(firstText);
    TSDocument secondSyntax;
    secondSyntax.setText(secondText);

    ScopedSearchPanelContext context;
    context.documents = {
        {firstFile, firstText, &firstSyntax, 7},
        {secondFile, secondText, &secondSyntax, 11},
    };
    context.activeFileName = firstFile;
    context.cursorChar =
        firstText.indexOf(QStringLiteral("target ="));

    const int declaration =
        firstText.indexOf(QStringLiteral("target"));
    SemanticIndex semanticIndex;
    semanticIndex.updateSymbolRecordsForFile(
        firstFile,
        {signalRecord(firstFile,
                      firstText,
                      declaration,
                      QStringLiteral("target"))},
        firstText);
    SearchService searchService(&semanticIndex);

    ScopedSearchPanel panel(&searchService);
    panel.setSearchContext(context);
    panel.setQueryText(QStringLiteral("target"));
    panel.setScope(ScopedSearchScope::Workspace);
    panel.refresh();

    check(panel.response().ready()
              && panel.response().results.size() == 5
              && panel.displayedResultCount() == 5
              && childCount(panel.resultsView()) == 5
              && panel.resultsView()->topLevelItemCount() == 2,
          "workspace results are grouped per file without duplicates");

    bool mergedOriginRenderedOnce = false;
    int declarationRows = 0;
    for (int fileIndex = 0;
         fileIndex
             < panel.resultsView()->topLevelItemCount();
         ++fileIndex) {
        QTreeWidgetItem* fileItem =
            panel.resultsView()->topLevelItem(fileIndex);
        for (int matchIndex = 0;
             matchIndex < fileItem->childCount();
             ++matchIndex) {
            QTreeWidgetItem* matchItem =
                fileItem->child(matchIndex);
            if (EditorFileIdentity::same(
                    fileItem->text(0), firstFile)
                && matchItem->text(0)
                    == QStringLiteral("2:9")) {
                ++declarationRows;
                mergedOriginRenderedOnce =
                    matchItem->text(2)
                    == QStringLiteral("Text + Semantic");
            }
        }
    }
    check(declarationRows == 1
              && mergedOriginRenderedOnce,
          "text and semantic hits render as one physical match");

    QTreeWidgetItem* navigatedItem =
        firstMatch(panel.resultsView());
    const QString stableContext =
        navigatedItem ? navigatedItem->text(1) : QString();
    firstSyntax.setText(
        QStringLiteral("module changed; endmodule\n"));
    check(navigatedItem
              && navigatedItem->text(1) == stableContext
              && !stableContext.isEmpty(),
          "rendered context remains the captured search snapshot");
    firstSyntax.setText(firstText);

    QString navigatedFile;
    int navigatedLine = 0;
    int navigatedColumn = 0;
    QObject::connect(
        &panel,
        &ScopedSearchPanel::navigationRequested,
        [&](const QString& fileName,
            int line,
            int column) {
            navigatedFile = fileName;
            navigatedLine = line;
            navigatedColumn = column;
        });
    const bool activationInvoked =
        navigatedItem
        && QMetaObject::invokeMethod(
            panel.resultsView(),
            "itemActivated",
            Qt::DirectConnection,
            Q_ARG(QTreeWidgetItem*, navigatedItem),
            Q_ARG(int, 0));
    check(activationInvoked
              && !navigatedFile.isEmpty()
              && navigatedLine > 0
              && navigatedColumn > 0,
          "result activation emits file, line, and column");

    EditorLocation temporarySearchLocation;
    QObject::connect(
        &panel,
        &ScopedSearchPanel::temporaryEditorOpenRequested,
        [&](const EditorLocation& location) {
            temporarySearchLocation = location;
        });
    const bool temporarySearchRequested =
        panel.requestTemporaryEditorOpenForItem(
            navigatedItem);
    check(temporarySearchRequested
              && panel.resultsView()->contextMenuPolicy()
                     == Qt::CustomContextMenu
              && temporarySearchLocation.isValid()
              && temporarySearchLocation.filePath
                     == navigatedFile
              && temporarySearchLocation.line
                     == navigatedLine
              && temporarySearchLocation.column
                     == navigatedColumn
              && !temporarySearchLocation.symbolKey.isEmpty()
              && !temporarySearchLocation.sourceLinkId.isEmpty()
              && temporarySearchLocation.selection
              && temporarySearchLocation.selection->isValid(),
          "Search result exposes a source-preserving temporary-editor context Action target");

    panel.setScope(ScopedSearchScope::SyntaxBlock);
    panel.refresh();
    check(panel.response().ready()
              && panel.displayedResultCount() == 2
              && panel.resultsView()->topLevelItemCount() == 1,
          "syntax-block scope uses the active cursor snapshot");

    panel.setScope(ScopedSearchScope::Workspace);
    panel.refresh();
    check(panel.replaceChecklist()->topLevelItemCount() == 2
              && panel.displayedReplaceMatchCount() == 5,
          "replace area exposes per-file and per-match checkboxes");

    QTreeWidgetItem* firstReplaceFile =
        fileItemFor(panel.replaceChecklist(), firstFile);
    QTreeWidgetItem* secondReplaceFile =
        fileItemFor(panel.replaceChecklist(), secondFile);
    check(firstReplaceFile && secondReplaceFile
              && (firstReplaceFile->flags()
                  & Qt::ItemIsUserCheckable)
              && firstReplaceFile->childCount() == 3
              && (firstReplaceFile->child(0)->flags()
                  & Qt::ItemIsUserCheckable)
              && secondReplaceFile->childCount() == 2,
          "both checklist levels are explicitly selectable");

    if (firstReplaceFile)
        firstReplaceFile->setCheckState(0, Qt::Unchecked);
    if (secondReplaceFile
        && secondReplaceFile->childCount() > 1) {
        secondReplaceFile->child(1)->setCheckState(
            0, Qt::Unchecked);
    }
    panel.setReplacementText(QStringLiteral("renamed"));

    const QString firstTextBeforePreview =
        context.documents.at(0).text;
    const QString secondTextBeforePreview =
        context.documents.at(1).text;
    const ReplacePreviewPlan preview =
        panel.buildReplacePreview();
    check(preview.ready()
              && preview.files.size() == 2
              && preview.transactionPlan.edits.size() == 1,
          "selected files and matches produce only selected edits");
    check(preview.ready()
              && preview.transactionPlan.riskLevel
                  == rtledit::RiskLevel::High
              && preview.transactionPlan.previewPolicy
                  == rtledit::PreviewPolicy::Diff,
          "replace planning exposes only a High-risk Diff plan");
    check(context.documents.at(0).text
                  == firstTextBeforePreview
              && context.documents.at(1).text
                  == secondTextBeforePreview,
          "building the replace preview does not apply edits");

    check(panel.applyButton()
              && panel.cancelButton()
              && panel.undoButton()
              && panel.diffView()
              && !panel.applyButton()->isEnabled()
              && !panel.cancelButton()->isEnabled()
              && !panel.undoButton()->isEnabled(),
          "replace transaction controls remain disabled until a "
          "workflow prepares the Diff");

    QMainWindow window;
    window.resize(900, 640);
    auto* editorFocus = new QLineEdit(&window);
    window.setCentralWidget(editorFocus);
    ScopedSearchPanelCoordinator coordinator(
        &window, &searchService);
    coordinator.setSearchContext(context);
    coordinator.panel()->setQueryText(
        QStringLiteral("target"));
    coordinator.panel()->setScope(
        ScopedSearchScope::Workspace);
    QString temporarySearchActionId;
    QVariantMap temporarySearchParameters;
    coordinator.setRegisteredActionRequestHandler(
        [&](const QString& actionId,
            const QVariantMap& parameters) {
            temporarySearchActionId = actionId;
            temporarySearchParameters = parameters;
            ActionExecutionResult result;
            result.handled = true;
            result.succeeded = true;
            return result;
        });
    coordinator.refresh();
    QTreeWidgetItem* coordinatorSearchItem =
        firstMatch(coordinator.panel()->resultsView());
    const bool coordinatorTemporaryRequest =
        coordinator.panel()
        && coordinator.panel()
               ->requestTemporaryEditorOpenForItem(
                   coordinatorSearchItem);
    check(coordinatorTemporaryRequest
              && temporarySearchActionId
                     == QString::fromLatin1(
                         ActionIds::ViewTemporaryEditorOpen)
              && temporarySearchParameters
                     .value(QStringLiteral("path"))
                     .toString()
                     == temporarySearchLocation.filePath
              && temporarySearchParameters
                     .value(QStringLiteral("line"))
                     .toInt()
                     == temporarySearchLocation.line
              && temporarySearchParameters
                     .value(QStringLiteral("column"))
                     .toInt()
                     == temporarySearchLocation.column
              && !temporarySearchParameters
                      .value(QStringLiteral("sourceLinkId"))
                      .toString()
                      .isEmpty(),
          "Search result dispatches the unified temporary-editor Registry Action");
    window.addDockWidget(
        Qt::BottomDockWidgetArea,
        coordinator.dock());

    PanelLayoutController layoutController(&window);
    check(layoutController.registerBottomPanel(
              ScopedSearchPanelCoordinator::panelId(),
              coordinator.dock()),
          "coordinator dock registers with panel layout");
    layoutController.finalize();

    window.show();
    QApplication::processEvents();
    window.resizeDocks(
        {coordinator.dock()},
        {190},
        Qt::Vertical);
    QApplication::processEvents();

    ScopedSearchPanel* const uniquePanel =
        coordinator.panel();
    QDockWidget* const uniqueDock =
        coordinator.dock();
    check(layoutController.closePanel(
              ScopedSearchPanelCoordinator::panelId()),
          "external panel layout closes the search page");
    QApplication::processEvents();
    const int closedHeight = uniqueDock->height();
    editorFocus->setFocus();
    QApplication::processEvents();
    QWidget* const focusBeforeClosedRefresh =
        QApplication::focusWidget();
    coordinator.refresh();
    QApplication::processEvents();
    check(coordinator.panel() == uniquePanel
              && coordinator.dock() == uniqueDock
              && uniqueDock->isHidden()
              && uniqueDock->height() == closedHeight
              && QApplication::focusWidget()
                  == focusBeforeClosedRefresh,
          "refresh reuses the hidden page without visibility, height, or focus changes");

    check(layoutController.restorePanel(
              ScopedSearchPanelCoordinator::panelId()),
          "external panel layout reopens the search page");
    QApplication::processEvents();
    window.resizeDocks(
        {uniqueDock},
        {205},
        Qt::Vertical);
    QApplication::processEvents();
    const int visibleHeight = uniqueDock->height();
    editorFocus->setFocus();
    QApplication::processEvents();
    QWidget* const focusBeforeVisibleRefresh =
        QApplication::focusWidget();
    coordinator.refresh();
    QApplication::processEvents();
    check(coordinator.panel() == uniquePanel
              && window.findChildren<ScopedSearchPanel*>()
                     .size() == 1
              && uniqueDock->isVisible()
              && uniqueDock->height() == visibleHeight
              && QApplication::focusWidget()
                  == focusBeforeVisibleRefresh,
          "visible refresh preserves the unique page, dock height, and editor focus");

    window.hide();
    MainWindow productionWindow;
    productionWindow.workspaceManager
        ->setRecentWorkspacePersistenceEnabledForTesting(
            false);
    check(productionWindow.workspaceManager
              ->openWorkspace(workspace.path())
              && productionWindow.workspaceManager
                     ->restoreSessionScanState(
                         {firstFile,
                          secondFile,
                          thirdFile},
                         true)
              && productionWindow.tabManager
                     ->openFileInTab(firstFile),
          "production window opens the scoped-search workspace");
    QApplication::processEvents();

    MyCodeEditor* activeEditor =
        productionWindow.tabManager
            ->getCurrentEditor();
    const QString dirtyFirstText =
        QStringLiteral(
            "module first;\n"
            "  logic buffer_only;\n"
            "endmodule\n");
    if (activeEditor) {
        activeEditor->setPlainText(dirtyFirstText);
        QTextCursor cursor =
            activeEditor->textCursor();
        cursor.setPosition(
            dirtyFirstText.indexOf(
                QStringLiteral("buffer_only")));
        activeEditor->setTextCursor(cursor);
    }
    QApplication::processEvents();

    SharedDocument* const activeDocument =
        productionWindow.tabManager
            ->sharedDocumentForEditor(activeEditor);
    const bool duplicatedView =
        productionWindow.tabManager
            ->duplicateCurrentView();
    QApplication::processEvents();
    ScopedSearchPanel* const productionPanel =
        productionWindow.findChild<ScopedSearchPanel*>(
            QStringLiteral("scopedSearchPanel"));
    QDockWidget* const productionDock =
        productionWindow.findChild<QDockWidget*>(
            QStringLiteral("scopedSearchDock"));
    check(productionPanel
              && productionDock
              && productionWindow
                         .findChildren<ScopedSearchPanel*>()
                         .size()
                     == 1
              && productionDock
                         ->property("bottomPanelId")
                         .toString()
                     == ScopedSearchPanelCoordinator::panelId()
              && !productionDock
                      ->toggleViewAction()
                      ->isChecked(),
          "production owns one registered scoped-search bottom page");
    check(activeEditor
              && activeDocument
              && activeEditor->syntaxDocument()
              && activeEditor->syntaxDocument()->text()
                     == dirtyFirstText,
          "active unsaved buffer exposes its current Tree-sitter snapshot");
    check(duplicatedView
              && activeDocument
              && activeDocument->viewCount() == 2,
          "production fixture has two views of one SharedDocument");

    if (productionPanel) {
        productionPanel->setScope(
            ScopedSearchScope::Workspace);
        productionPanel->setQueryText(
            QStringLiteral("disk_only"));
        productionPanel->refresh();
    }
    check(productionPanel
              && productionPanel->response().ready()
              && productionPanel->displayedResultCount()
                     == 1
              && EditorFileIdentity::same(
                     productionPanel->response()
                         .results.first()
                         .fileName,
                     thirdFile),
          "workspace search includes an unopened workspace file");

    if (productionPanel) {
        productionPanel->setQueryText(
            QStringLiteral("disk_stale"));
        productionPanel->refresh();
    }
    check(productionPanel
              && productionPanel->response().ready()
              && productionPanel->displayedResultCount()
                     == 0,
          "open dirty text overrides stale disk content");

    if (productionPanel) {
        productionPanel->setQueryText(
            QStringLiteral("buffer_only"));
        productionPanel->refresh();
    }
    check(productionPanel
              && productionPanel->response().ready()
              && productionPanel->displayedResultCount()
                     == 1
              && activeDocument
              && productionPanel->response()
                         .results.first()
                         .context.documentRevision
                     == activeDocument->textRevision(),
          "workspace search uses the SharedDocument text and revision");

    if (productionPanel) {
        productionPanel->setScope(
            ScopedSearchScope::Module);
        productionPanel->refresh();
    }
    check(productionPanel
              && productionPanel->response().ready()
              && productionPanel->displayedResultCount()
                     == 1,
          "module search uses the active unsaved Tree-sitter document");

    if (productionPanel) {
        productionPanel->setScope(
            ScopedSearchScope::Workspace);
        productionPanel->setQueryText(
            QStringLiteral("disk_only"));
        productionPanel->refresh();
    }
    QTreeWidgetItem* const productionNavigationItem =
        productionPanel
        ? firstMatch(productionPanel->resultsView())
        : nullptr;
    const bool productionNavigationInvoked =
        productionPanel
        && productionNavigationItem
        && QMetaObject::invokeMethod(
            productionPanel->resultsView(),
            "itemActivated",
            Qt::DirectConnection,
            Q_ARG(QTreeWidgetItem*,
                  productionNavigationItem),
            Q_ARG(int, 0));
    QApplication::processEvents();
    MyCodeEditor* const navigatedEditor =
        productionWindow.tabManager
            ->getCurrentEditor();
    check(productionNavigationInvoked
              && navigatedEditor
              && EditorFileIdentity::same(
                     navigatedEditor
                         ->documentFileName(),
                     thirdFile),
          "production result navigation uses the existing source jump path");

    productionWindow.tabManager->createNewTab();
    MyCodeEditor* const untitledEditor =
        productionWindow.tabManager
            ->getCurrentEditor();
    const QString untitledText =
        QStringLiteral(
            "module scratch;\n"
            "  logic untitled_only;\n"
            "endmodule\n");
    if (untitledEditor) {
        untitledEditor->setPlainText(untitledText);
        QTextCursor cursor =
            untitledEditor->textCursor();
        cursor.setPosition(
            untitledText.indexOf(
                QStringLiteral("untitled_only")));
        untitledEditor->setTextCursor(cursor);
    }
    QApplication::processEvents();
    if (productionPanel) {
        productionPanel->setScope(
            ScopedSearchScope::Workspace);
        productionPanel->setQueryText(
            QStringLiteral("untitled_only"));
        productionPanel->refresh();
    }
    const bool untitledExcludedFromWorkspace =
        productionPanel
        && productionPanel->response().ready()
        && productionPanel->displayedResultCount() == 0;
    if (productionPanel) {
        productionPanel->setScope(
            ScopedSearchScope::Module);
        productionPanel->refresh();
    }
    check(untitledExcludedFromWorkspace
              && productionPanel
              && productionPanel->response().ready()
              && productionPanel->displayedResultCount()
                     == 1,
          "active untitled text supports current scope but is excluded from workspace results");

    std::cout << "scoped_search_panel_test: "
              << (checks - failures) << '/' << checks
              << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
