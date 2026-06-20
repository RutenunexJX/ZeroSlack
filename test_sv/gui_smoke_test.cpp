// Offscreen GUI smoke test for the real MainWindow/TabManager/MyCodeEditor path.
// It keeps the assertions coarse on purpose: this target is a repeatable guard that
// the GUI workflow is alive, while detailed semantic behavior stays in the focused
// headless tests.
#include <QApplication>
#include <QAction>
#include <QCompleter>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextStream>
#include <QTimer>
#include <QTreeWidget>
#include <QtTest/QTest>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <utility>

#define private public
#include "mainwindow.h"
#include "analysisscheduler.h"
#include "alternatecommandservice.h"
#include "documentmodel.h"
#include "editorsemanticcontextservice.h"
#include "filecommandcoordinator.h"
#include "navigationwidget.h"
#include "semantic_fixture_records.h"
#include "navigationmanager.h"
#include "problemspanelcoordinator.h"
#include "referencespanelcoordinator.h"
#include "relationshipspanelcoordinator.h"
#include "rtlinsightspanelcoordinator.h"
#include "semanticindex.h"
#include "semanticindexsnapshot.h"
#include "semanticdockcoordinator.h"
#include "semanticpanelrefreshcoordinator.h"
#include "semanticruntimecoordinator.h"
#include "smartrelationshipbuilder.h"
#include "tabmanager.h"
#include "workspacemanager.h"
#undef private

static int g_checks = 0;
static int g_fails = 0;

static QSet<QString> packageScopeNames(const QList<sym_list::SymbolInfo>& symbols)
{
    QSet<QString> names;
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (SymbolTaxonomy::isPackageDeclaration(
                semanticMetadataForSymbolInfo(symbol))
            && !symbol.symbolName.isEmpty()) {
            names.insert(symbol.symbolName);
        }
    }
    return names;
}

static std::shared_ptr<const SemanticIndexSnapshot> snapshotFromSymbols(
    const QList<sym_list::SymbolInfo>& symbols,
    QList<SemanticRelationship> relationships = {},
    QList<SemanticDiagnostic> diagnostics = {},
    QHash<QString, QString> fileContents = {})
{
    return std::make_shared<const SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(
            semanticSymbolRecordsForSymbols(
                symbols,
                packageScopeNames(symbols)),
            std::move(relationships),
            std::move(diagnostics),
            std::move(fileContents)));
}

static void expectBool(const char* what, bool got, bool want)
{
    ++g_checks;
    const bool ok = (got == want);
    if (!ok)
        ++g_fails;
    printf("[%s] %-48s got=%s want=%s\n",
           ok ? "PASS" : "FAIL", what, got ? "true" : "false", want ? "true" : "false");
    fflush(stdout);
}

static bool waitUntil(const std::function<bool()>& predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (predicate())
            return true;
        QTest::qWait(20);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    return predicate();
}

static QString largestFile(const QStringList& files)
{
    QStringList sorted = files;
    std::sort(sorted.begin(), sorted.end(), [](const QString& a, const QString& b) {
        return QFileInfo(a).size() > QFileInfo(b).size();
    });
    return sorted.isEmpty() ? QString() : sorted.first();
}

static QTextBlock findBlockContaining(QTextDocument* doc, const QString& needle)
{
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        if (block.text().contains(needle))
            return block;
    }
    return QTextBlock();
}

static QTreeWidgetItem* findItemByText(QTreeWidgetItem* item, const QString& text)
{
    if (!item)
        return nullptr;
    if (item->text(0) == text)
        return item;
    for (int i = 0; i < item->childCount(); ++i) {
        if (QTreeWidgetItem* found = findItemByText(item->child(i), text))
            return found;
    }
    return nullptr;
}

static QTreeWidgetItem* findItemByText(QTreeWidget* tree, const QString& text)
{
    if (!tree)
        return nullptr;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (QTreeWidgetItem* found = findItemByText(tree->topLevelItem(i), text))
            return found;
    }
    return nullptr;
}

static int navigableItemCount(QTreeWidgetItem* item)
{
    if (!item)
        return 0;
    int count = item->data(0, Qt::UserRole).toString().isEmpty() ? 0 : 1;
    for (int i = 0; i < item->childCount(); ++i)
        count += navigableItemCount(item->child(i));
    return count;
}

static int navigableItemCount(QTreeWidget* tree)
{
    if (!tree)
        return 0;
    int count = 0;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        count += navigableItemCount(tree->topLevelItem(i));
    return count;
}

static bool hasNavigableFile(QTreeWidgetItem* item, const QString& fileName)
{
    if (!item)
        return false;
    const QString itemFileName = item->data(0, Qt::UserRole).toString();
    if (!itemFileName.isEmpty()
        && QFileInfo(itemFileName).absoluteFilePath() == QFileInfo(fileName).absoluteFilePath()) {
        return true;
    }
    for (int i = 0; i < item->childCount(); ++i) {
        if (hasNavigableFile(item->child(i), fileName))
            return true;
    }
    return false;
}

static bool hasNavigableFile(QTreeWidget* tree, const QString& fileName)
{
    if (!tree)
        return false;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (hasNavigableFile(tree->topLevelItem(i), fileName))
            return true;
    }
    return false;
}

static QTreeWidgetItem* firstNavigableItem(QTreeWidgetItem* item)
{
    if (!item)
        return nullptr;
    if (!item->data(0, Qt::UserRole).toString().isEmpty())
        return item;
    for (int i = 0; i < item->childCount(); ++i) {
        if (QTreeWidgetItem* found = firstNavigableItem(item->child(i)))
            return found;
    }
    return nullptr;
}

static QTreeWidgetItem* firstNavigableItem(QTreeWidget* tree)
{
    if (!tree)
        return nullptr;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (QTreeWidgetItem* found = firstNavigableItem(tree->topLevelItem(i)))
            return found;
    }
    return nullptr;
}

static void collectNavigableItems(QTreeWidgetItem* item, QList<QTreeWidgetItem*>& out)
{
    if (!item)
        return;
    if (!item->data(0, Qt::UserRole).toString().isEmpty())
        out.append(item);
    for (int i = 0; i < item->childCount(); ++i)
        collectNavigableItems(item->child(i), out);
}

static QList<QTreeWidgetItem*> navigableItems(QTreeWidget* tree)
{
    QList<QTreeWidgetItem*> out;
    if (!tree)
        return out;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        collectNavigableItems(tree->topLevelItem(i), out);
    return out;
}

static QTreeWidget* problemsTree(MainWindow& window)
{
    return window.semanticDocks && window.semanticDocks->problemsPanelCoordinator()
        ? window.semanticDocks->problemsPanelCoordinator()->tree()
        : nullptr;
}

static QComboBox* problemsScopeCombo(MainWindow& window)
{
    return window.semanticDocks && window.semanticDocks->problemsPanelCoordinator()
        ? window.semanticDocks->problemsPanelCoordinator()->scopeCombo()
        : nullptr;
}

static QTreeWidget* referencesTree(MainWindow& window)
{
    return window.semanticDocks && window.semanticDocks->referencesPanelCoordinator()
        ? window.semanticDocks->referencesPanelCoordinator()->tree()
        : nullptr;
}

static QComboBox* referenceScopeCombo(MainWindow& window)
{
    return window.semanticDocks && window.semanticDocks->referencesPanelCoordinator()
        ? window.semanticDocks->referencesPanelCoordinator()->scopeCombo()
        : nullptr;
}

static QComboBox* referenceTypeCombo(MainWindow& window)
{
    return window.semanticDocks && window.semanticDocks->referencesPanelCoordinator()
        ? window.semanticDocks->referencesPanelCoordinator()->typeCombo()
        : nullptr;
}

static QTreeWidget* relationshipsTree(MainWindow& window)
{
    return window.semanticDocks && window.semanticDocks->relationshipsPanelCoordinator()
        ? window.semanticDocks->relationshipsPanelCoordinator()->tree()
        : nullptr;
}

static QTreeWidget* rtlInsightsTree(MainWindow& window)
{
    return window.semanticDocks && window.semanticDocks->rtlInsightsPanelCoordinator()
        ? window.semanticDocks->rtlInsightsPanelCoordinator()->tree()
        : nullptr;
}

static QComboBox* relationshipViewCombo(MainWindow& window)
{
    return window.semanticDocks && window.semanticDocks->relationshipsPanelCoordinator()
        ? window.semanticDocks->relationshipsPanelCoordinator()->viewCombo()
        : nullptr;
}

static QComboBox* relationshipDirectionCombo(MainWindow& window)
{
    return window.semanticDocks && window.semanticDocks->relationshipsPanelCoordinator()
        ? window.semanticDocks->relationshipsPanelCoordinator()->directionCombo()
        : nullptr;
}

static QComboBox* relationshipTypeCombo(MainWindow& window)
{
    return window.semanticDocks && window.semanticDocks->relationshipsPanelCoordinator()
        ? window.semanticDocks->relationshipsPanelCoordinator()->typeCombo()
        : nullptr;
}

static QComboBox* relationshipDepthCombo(MainWindow& window)
{
    return window.semanticDocks && window.semanticDocks->relationshipsPanelCoordinator()
        ? window.semanticDocks->relationshipsPanelCoordinator()->depthCombo()
        : nullptr;
}

static SemanticPanelRefreshCoordinator* semanticPanelRefresh(MainWindow& window)
{
    return window.semanticDocks ? window.semanticDocks->refreshCoordinator() : nullptr;
}

static void drainRelationshipWork(MainWindow& window)
{
    SmartRelationshipBuilder* builder = window.semanticRuntime
        ? window.semanticRuntime->relationshipBuilder()
        : nullptr;
    if (builder)
        builder->cancelAnalysis();
    if (window.analysisScheduler) {
        window.analysisScheduler->cancelAllScheduledRelationshipAnalyses();
        window.analysisScheduler->cancelRelationshipAnalysis();
        window.analysisScheduler->cancelWorkspaceRelationshipAnalysis();
    }
}

static void runReferenceDockRegression(MainWindow& window, const QString& fixturePath)
{
    printf("\n-- reference dock regression --\n");

    sym_list::SymbolInfo referenced;
    referenced.fileName = fixturePath;
    referenced.symbolName = QStringLiteral("target_ref");
    referenced.symbolType = sym_list::sym_logic;
    referenced.startLine = 3;
    referenced.startColumn = 9;
    referenced.endLine = 3;
    referenced.endColumn = 18;
    referenced.position = 0;
    referenced.length = 10;
    referenced.symbolId = 9001;
    referenced.moduleScope = QStringLiteral("ref_top");

    sym_list::SymbolInfo referencing;
    referencing.fileName = fixturePath;
    referencing.symbolName = QStringLiteral("source_ref");
    referencing.symbolType = sym_list::sym_assign;
    referencing.startLine = 8;
    referencing.startColumn = 3;
    referencing.endLine = 8;
    referencing.endColumn = 20;
    referencing.position = 0;
    referencing.length = 10;
    referencing.symbolId = 9002;
    referencing.moduleScope = QStringLiteral("ref_top");

    sym_list::SymbolInfo externalReferencing;
    externalReferencing.fileName = fixturePath + QStringLiteral(".refs.sv");
    externalReferencing.symbolName = QStringLiteral("external_ref");
    externalReferencing.symbolType = sym_list::sym_assign;
    externalReferencing.startLine = 4;
    externalReferencing.startColumn = 5;
    externalReferencing.endLine = 4;
    externalReferencing.endColumn = 22;
    externalReferencing.position = 0;
    externalReferencing.length = 12;
    externalReferencing.symbolId = 9004;
    externalReferencing.moduleScope = QStringLiteral("ref_external");

    sym_list::SymbolInfo target;
    target.fileName = fixturePath;
    target.symbolName = QStringLiteral("target_sink");
    target.symbolType = sym_list::sym_function;
    target.startLine = 12;
    target.startColumn = 12;
    target.endLine = 12;
    target.endColumn = 22;
    target.position = 0;
    target.length = 11;
    target.symbolId = 9003;
    target.moduleScope = QStringLiteral("ref_top");

    SemanticRelationship incomingRelationship;
    incomingRelationship.fromId = referencing.symbolId;
    incomingRelationship.toId = referenced.symbolId;
    incomingRelationship.type = SymbolRelationshipEngine::REFERENCES;

    SemanticRelationship externalIncomingRelationship;
    externalIncomingRelationship.fromId = externalReferencing.symbolId;
    externalIncomingRelationship.toId = referenced.symbolId;
    externalIncomingRelationship.type = SymbolRelationshipEngine::READS_FROM;

    SemanticRelationship outgoingRelationship;
    outgoingRelationship.fromId = referenced.symbolId;
    outgoingRelationship.toId = target.symbolId;
    outgoingRelationship.type = SymbolRelationshipEngine::CALLS;

    SemanticIndex::getInstance()->setSnapshot(
        snapshotFromSymbols(
            QList<sym_list::SymbolInfo>{referenced, referencing, externalReferencing, target},
            QList<SemanticRelationship>{incomingRelationship,
                                        externalIncomingRelationship,
                                        outgoingRelationship}));

    semanticPanelRefresh(window)->showReferencesForSymbol(QStringLiteral("target_ref"),
                                                          fixturePath,
                                                          QStringLiteral("ref_top"));

    expectBool("references tree exists", referencesTree(window) != nullptr, true);
    expectBool("reference results rendered",
               navigableItemCount(referencesTree(window)) == 2,
               true);
    expectBool("reference scope filter exists",
               referenceScopeCombo(window) != nullptr,
               true);
    expectBool("reference type filter exists",
               referenceTypeCombo(window) != nullptr,
               true);
    if (referenceScopeCombo(window)) {
        referenceScopeCombo(window)->setCurrentIndex(
            referenceScopeCombo(window)->findText(QStringLiteral("Workspace Files")));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        expectBool("reference workspace scope hides non-workspace files",
                   navigableItemCount(referencesTree(window)) == 0,
                   true);
    }
    if (referenceScopeCombo(window)) {
        referenceScopeCombo(window)->setCurrentIndex(
            referenceScopeCombo(window)->findText(QStringLiteral("Current File")));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        expectBool("reference scope narrows to current file",
                   navigableItemCount(referencesTree(window)) == 1,
                   true);
    }
    if (referencesTree(window) && navigableItemCount(referencesTree(window)) == 1) {
        QTreeWidgetItem* item = firstNavigableItem(referencesTree(window));
        expectBool("reference row uses source symbol",
                   item && item->text(0) == QStringLiteral("source_ref"),
                   true);
        expectBool("reference row stores source line",
                   item && item->data(0, Qt::UserRole + 1).toInt() == referencing.startLine,
                   true);
    }
    if (referenceScopeCombo(window) && referenceTypeCombo(window)) {
        referenceScopeCombo(window)->setCurrentIndex(
            referenceScopeCombo(window)->findText(QStringLiteral("All Files")));
        referenceTypeCombo(window)->setCurrentIndex(
            referenceTypeCombo(window)->findText(QStringLiteral("Reads From")));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        expectBool("reference type filter narrows results",
                   navigableItemCount(referencesTree(window)) == 1,
                   true);
        QTreeWidgetItem* item = firstNavigableItem(referencesTree(window));
        expectBool("reference type filter keeps external source",
                   item && item->text(0) == QStringLiteral("external_ref"),
                   true);
        referenceTypeCombo(window)->setCurrentIndex(
            referenceTypeCombo(window)->findText(QStringLiteral("All Types")));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }

    MyCodeEditor shortcutEditor;
    shortcutEditor.setPlainText(
        "module ref_top;\n"
        "  logic target_ref;\n"
        "endmodule\n");
    DocumentModel shortcutDocumentModel;
    shortcutDocumentModel.registerEditor(&shortcutEditor, fixturePath);
    const int targetOffset = shortcutEditor.toPlainText().indexOf(QStringLiteral("target_ref")) + 2;
    QTextCursor shortcutCursor(shortcutEditor.document());
    shortcutCursor.setPosition(targetOffset);
    shortcutEditor.setTextCursor(shortcutCursor);
    int sourceActionCount = 0;
    SourceSymbolAction lastSourceAction = SourceSymbolAction::FindReferences;
    EditorSemanticContext lastSourceActionContext;
    QObject::connect(&shortcutEditor,
                     &MyCodeEditor::sourceSymbolActionRequested,
                     &shortcutEditor,
                     [&](SourceSymbolAction action,
                         const EditorSemanticContext& context) {
                         ++sourceActionCount;
                         lastSourceAction = action;
                         lastSourceActionContext = context;
                     });
    QTest::keyClick(&shortcutEditor, Qt::Key_F12, Qt::ShiftModifier);
    expectBool("find references shortcut emits request",
               sourceActionCount == 1
                   && lastSourceAction == SourceSymbolAction::FindReferences,
               true);
    expectBool("find references shortcut emits symbol",
               lastSourceActionContext.lineText.contains(QStringLiteral("target_ref")),
               true);
    QTest::keyClick(&shortcutEditor, Qt::Key_R,
                    Qt::ControlModifier | Qt::ShiftModifier);
    expectBool("show relationships shortcut emits request",
               sourceActionCount == 2
                   && lastSourceAction == SourceSymbolAction::ShowRelationships,
               true);
    expectBool("show relationships shortcut emits symbol",
               lastSourceActionContext.lineText.contains(QStringLiteral("target_ref"))
                   && lastSourceActionContext.fileName == fixturePath
                   && lastSourceActionContext.moduleName == QStringLiteral("ref_top"),
               true);

    QString emittedAlternateCommand;
    QObject::connect(&shortcutEditor,
                     &MyCodeEditor::alternateCommandRequested,
                     &shortcutEditor,
                     [&](const QString& command) {
                         emittedAlternateCommand = command;
                     });
    shortcutEditor.executeAlternateModeCommand(QStringLiteral("save"));
    expectBool("alternate command emits command request",
               emittedAlternateCommand == QStringLiteral("save"),
               true);

    shortcutEditor.clear();
    FileCommandCoordinator editorCommandCoordinator(nullptr, nullptr);
    editorCommandCoordinator.executeAlternateCommandText(
        &shortcutEditor, QStringLiteral("comment"));
    expectBool("file coordinator executes command text",
               shortcutEditor.toPlainText() == QStringLiteral("// "),
               true);

    shortcutEditor.clear();
    editorCommandCoordinator.executeAlternateCommand(&shortcutEditor,
                                                    AlternateCommandAction::Comment);
    expectBool("file coordinator executes editor command",
               shortcutEditor.toPlainText() == QStringLiteral("// "),
               true);

    semanticPanelRefresh(window)->showRelationshipsForSymbol(QStringLiteral("target_ref"),
                                                             fixturePath,
                                                             QStringLiteral("ref_top"));
    expectBool("relationships tree exists", relationshipsTree(window) != nullptr, true);
    expectBool("relationship results rendered",
               navigableItemCount(relationshipsTree(window)) == 3,
               true);
    if (relationshipsTree(window) && navigableItemCount(relationshipsTree(window)) == 3) {
        bool sawIncoming = false;
        bool sawOutgoing = false;
        bool sawExternal = false;
        bool sawExplanation = false;
        const QList<QTreeWidgetItem*> items = navigableItems(relationshipsTree(window));
        for (QTreeWidgetItem* item : items) {
            sawIncoming = sawIncoming
                || (item->text(0) == QStringLiteral("Incoming")
                    && item->text(1) == QStringLiteral("source_ref"));
            sawOutgoing = sawOutgoing
                || (item->text(0) == QStringLiteral("Outgoing")
                    && item->text(1) == QStringLiteral("target_sink"));
            sawExternal = sawExternal
                || (item->text(0) == QStringLiteral("Incoming")
                    && item->text(1) == QStringLiteral("external_ref"));
            sawExplanation = sawExplanation
                || item->text(5) == QStringLiteral("source_ref references target_ref");
        }
        expectBool("incoming relationship row rendered", sawIncoming, true);
        expectBool("outgoing relationship row rendered", sawOutgoing, true);
        expectBool("external relationship row rendered", sawExternal, true);
        expectBool("relationship explanation column rendered", sawExplanation, true);
    }

    expectBool("relationship direction filter exists",
               relationshipDirectionCombo(window) != nullptr,
               true);
    expectBool("relationship type filter exists",
               relationshipTypeCombo(window) != nullptr,
               true);
    expectBool("relationship view filter exists",
               relationshipViewCombo(window) != nullptr,
               true);
    expectBool("relationship depth filter exists",
               relationshipDepthCombo(window) != nullptr,
               true);
    if (relationshipDirectionCombo(window) && relationshipTypeCombo(window)) {
        relationshipDirectionCombo(window)->setCurrentIndex(
            relationshipDirectionCombo(window)->findText(QStringLiteral("Outgoing")));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        expectBool("outgoing filter narrows relationships",
                   navigableItemCount(relationshipsTree(window)) == 1,
                   true);
        if (relationshipsTree(window) && navigableItemCount(relationshipsTree(window)) == 1) {
            QTreeWidgetItem* item = firstNavigableItem(relationshipsTree(window));
            expectBool("outgoing filter keeps target",
                       item && item->text(1) == QStringLiteral("target_sink"),
                       true);
        }

        relationshipDirectionCombo(window)->setCurrentIndex(
            relationshipDirectionCombo(window)->findText(QStringLiteral("All Directions")));
        relationshipTypeCombo(window)->setCurrentIndex(
            relationshipTypeCombo(window)->findText(QStringLiteral("References")));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        expectBool("type filter narrows relationships",
                   navigableItemCount(relationshipsTree(window)) == 1,
                   true);
        if (relationshipsTree(window) && navigableItemCount(relationshipsTree(window)) == 1) {
            QTreeWidgetItem* item = firstNavigableItem(relationshipsTree(window));
            expectBool("type filter keeps incoming source",
                       item && item->text(1) == QStringLiteral("source_ref"),
                       true);
        }
    }
    if (relationshipViewCombo(window) && relationshipTypeCombo(window)
        && relationshipDepthCombo(window)) {
        relationshipTypeCombo(window)->setCurrentIndex(
            relationshipTypeCombo(window)->findText(QStringLiteral("Calls")));
        relationshipDepthCombo(window)->setCurrentIndex(
            relationshipDepthCombo(window)->findText(QStringLiteral("Depth 2")));
        relationshipViewCombo(window)->setCurrentIndex(
            relationshipViewCombo(window)->findText(QStringLiteral("Tree")));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        expectBool("relationship tree mode renders hierarchy",
                   navigableItemCount(relationshipsTree(window)) == 2,
                   true);
        bool sawTreeRoot = false;
        bool sawTreeChild = false;
        const QList<QTreeWidgetItem*> items = navigableItems(relationshipsTree(window));
        for (QTreeWidgetItem* item : items) {
            sawTreeRoot = sawTreeRoot
                || (item->text(0) == QStringLiteral("Root")
                    && item->text(1) == QStringLiteral("target_ref"));
            sawTreeChild = sawTreeChild
                || (item->text(0) == QStringLiteral("Outgoing")
                    && item->text(1) == QStringLiteral("target_sink"));
        }
        expectBool("relationship tree mode keeps root", sawTreeRoot, true);
        expectBool("relationship tree mode keeps child target", sawTreeChild, true);
        expectBool("relationship tree keeps direction filter enabled",
                   relationshipDirectionCombo(window)->isEnabled(),
                   true);
        relationshipDirectionCombo(window)->setCurrentIndex(
            relationshipDirectionCombo(window)->findText(QStringLiteral("Incoming")));
        relationshipTypeCombo(window)->setCurrentIndex(
            relationshipTypeCombo(window)->findText(QStringLiteral("Reads From")));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        expectBool("relationship tree incoming filter renders hierarchy",
                   navigableItemCount(relationshipsTree(window)) == 2,
                   true);
        bool sawIncomingTreeSource = false;
        const QList<QTreeWidgetItem*> incomingItems = navigableItems(relationshipsTree(window));
        for (QTreeWidgetItem* item : incomingItems) {
            sawIncomingTreeSource = sawIncomingTreeSource
                || (item->text(0) == QStringLiteral("Incoming")
                    && item->text(1) == QStringLiteral("external_ref"));
        }
        expectBool("relationship tree keeps incoming source",
                   sawIncomingTreeSource, true);
        QTreeWidgetItem* rootItem = nullptr;
        for (QTreeWidgetItem* item : incomingItems) {
            if (item->text(0) == QStringLiteral("Root")
                && item->text(1) == QStringLiteral("target_ref")) {
                rootItem = item;
                break;
            }
        }
        expectBool("relationship tree root found for expansion state",
                   rootItem != nullptr, true);
        if (rootItem) {
            rootItem->setExpanded(false);
            semanticPanelRefresh(window)->refreshRelationshipsPanel();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QTreeWidgetItem* refreshedRoot = nullptr;
            const QList<QTreeWidgetItem*> refreshedItems =
                navigableItems(relationshipsTree(window));
            for (QTreeWidgetItem* item : refreshedItems) {
                if (item->text(0) == QStringLiteral("Root")
                    && item->text(1) == QStringLiteral("target_ref")) {
                    refreshedRoot = item;
                    break;
                }
            }
            expectBool("relationship tree preserves collapsed root",
                       refreshedRoot && !refreshedRoot->isExpanded(),
                       true);
        }
    }
}

static sym_list::SymbolInfo makeGuiSmokeSymbol(
    int id,
    const QString& fileName,
    const QString& name,
    sym_list::sym_type_e type,
    int line,
    const QString& moduleScope = QString())
{
    sym_list::SymbolInfo symbol;
    symbol.symbolId = id;
    symbol.fileName = fileName;
    symbol.symbolName = name;
    symbol.symbolType = type;
    symbol.startLine = line;
    symbol.endLine = line;
    symbol.startColumn = 1;
    symbol.endColumn = 1;
    symbol.position = 0;
    symbol.length = name.length();
    symbol.moduleScope = moduleScope;
    return symbol;
}

static void runRtlInsightsPanelRegression(MainWindow& window, const QString& fixturePath)
{
    printf("\n-- RTL insights panel regression --\n");

    QList<sym_list::SymbolInfo> symbols;
    sym_list::SymbolInfo module = makeGuiSmokeSymbol(
        9601,
        fixturePath,
        QStringLiteral("insight_top"),
        sym_list::sym_module,
        1);
    module.endLine = 18;
    symbols.append(module);
    symbols.append(makeGuiSmokeSymbol(
        9602,
        fixturePath,
        QStringLiteral("clk"),
        sym_list::sym_port_input,
        2,
        QStringLiteral("insight_top")));
    symbols.append(makeGuiSmokeSymbol(
        9603,
        fixturePath,
        QStringLiteral("rst_n"),
        sym_list::sym_port_input,
        3,
        QStringLiteral("insight_top")));
    symbols.append(makeGuiSmokeSymbol(
        9604,
        fixturePath,
        QStringLiteral("u_stage"),
        sym_list::sym_inst,
        8,
        QStringLiteral("insight_top")));

    sym_list::SymbolInfo stateQ = makeGuiSmokeSymbol(
        9605,
        fixturePath,
        QStringLiteral("state_q"),
        sym_list::sym_enum_var,
        10,
        QStringLiteral("insight_top"));
    stateQ.dataType = QStringLiteral("state_t");
    symbols.append(stateQ);
    sym_list::SymbolInfo stateD = makeGuiSmokeSymbol(
        9606,
        fixturePath,
        QStringLiteral("state_d"),
        sym_list::sym_enum_var,
        11,
        QStringLiteral("insight_top"));
    stateD.dataType = QStringLiteral("state_t");
    symbols.append(stateD);
    sym_list::SymbolInfo idle = makeGuiSmokeSymbol(
        9607,
        fixturePath,
        QStringLiteral("IDLE"),
        sym_list::sym_enum_value,
        5,
        QStringLiteral("insight_top"));
    idle.dataType = QStringLiteral("state_t");
    symbols.append(idle);
    sym_list::SymbolInfo run = makeGuiSmokeSymbol(
        9608,
        fixturePath,
        QStringLiteral("RUN"),
        sym_list::sym_enum_value,
        5,
        QStringLiteral("insight_top"));
    run.dataType = QStringLiteral("state_t");
    symbols.append(run);
    symbols.append(makeGuiSmokeSymbol(
        9609,
        fixturePath,
        QStringLiteral("data_q"),
        sym_list::sym_logic,
        12,
        QStringLiteral("insight_top")));
    symbols.append(makeGuiSmokeSymbol(
        9610,
        fixturePath,
        QStringLiteral("next_data"),
        sym_list::sym_logic,
        13,
        QStringLiteral("insight_top")));
    symbols.append(makeGuiSmokeSymbol(
        9611,
        fixturePath,
        QStringLiteral("consumer"),
        sym_list::sym_always_ff,
        14,
        QStringLiteral("insight_top")));
    symbols.append(makeGuiSmokeSymbol(
        9612,
        fixturePath,
        QStringLiteral("u_stage.data_i"),
        sym_list::sym_inst_pin,
        15,
        QStringLiteral("insight_top")));
    symbols.append(makeGuiSmokeSymbol(
        9613,
        fixturePath,
        QStringLiteral("scan_clk"),
        sym_list::sym_port_input,
        16,
        QStringLiteral("insight_top")));
    symbols.append(makeGuiSmokeSymbol(
        9614,
        fixturePath,
        QStringLiteral("insight_pkg"),
        sym_list::sym_package,
        18));
    symbols.append(makeGuiSmokeSymbol(
        9617,
        fixturePath,
        QStringLiteral("PKG_DEPTH"),
        sym_list::sym_parameter,
        21,
        QStringLiteral("insight_pkg")));
    symbols.append(makeGuiSmokeSymbol(
        9615,
        fixturePath,
        QStringLiteral("insight_if"),
        sym_list::sym_interface,
        19));
    sym_list::SymbolInfo interfaceBus = makeGuiSmokeSymbol(
        9616,
        fixturePath,
        QStringLiteral("if_bus"),
        sym_list::sym_inst,
        20,
        QStringLiteral("insight_top"));
    interfaceBus.dataType = QStringLiteral("insight_if");
    symbols.append(interfaceBus);

    QList<SemanticRelationship> relationships;
    SemanticRelationship packageRel;
    packageRel.fromId = 9601;
    packageRel.toId = 9614;
    packageRel.type = SymbolRelationshipEngine::REFERENCES;
    relationships.append(packageRel);
    SemanticRelationship clockRel;
    clockRel.fromId = 9602;
    clockRel.toId = 9601;
    clockRel.type = SymbolRelationshipEngine::CLOCKS;
    relationships.append(clockRel);
    SemanticRelationship resetRel;
    resetRel.fromId = 9603;
    resetRel.toId = 9601;
    resetRel.type = SymbolRelationshipEngine::RESETS;
    relationships.append(resetRel);
    SemanticRelationship assignRel;
    assignRel.fromId = 9610;
    assignRel.toId = 9609;
    assignRel.type = SymbolRelationshipEngine::ASSIGNS_TO;
    relationships.append(assignRel);
    SemanticRelationship readRel;
    readRel.fromId = 9611;
    readRel.toId = 9609;
    readRel.type = SymbolRelationshipEngine::READS_FROM;
    relationships.append(readRel);
    SemanticRelationship portRel;
    portRel.fromId = 9612;
    portRel.toId = 9609;
    portRel.type = SymbolRelationshipEngine::REFERENCES;
    relationships.append(portRel);
    SemanticRelationship interfaceRel;
    interfaceRel.fromId = 9616;
    interfaceRel.toId = 9609;
    interfaceRel.type = SymbolRelationshipEngine::REFERENCES;
    relationships.append(interfaceRel);
    SemanticRelationship instRel;
    instRel.fromId = 9601;
    instRel.toId = 9604;
    instRel.type = SymbolRelationshipEngine::INSTANTIATES;
    relationships.append(instRel);

    const QString content = QStringLiteral(
        "module insight_top(input logic clk, input logic rst_n);\n"
        "  typedef enum logic {IDLE, RUN} state_t;\n"
        "  state_t state_q;\n"
        "  state_t state_d;\n"
        "  always_comb begin\n"
        "    case (state_q)\n"
        "      IDLE: state_d = RUN;\n"
        "      RUN: state_d = IDLE;\n"
        "    endcase\n"
        "  end\n"
        "  input logic scan_clk;\n"
        "endmodule\n");
    QHash<QString, QString> fileContents;
    fileContents.insert(fixturePath, content);
    SemanticDiagnostic insightDiagnostic;
    insightDiagnostic.fileName = fixturePath;
    insightDiagnostic.line = 6;
    insightDiagnostic.column = 5;
    insightDiagnostic.message = QStringLiteral("insight warning");
    insightDiagnostic.severity = SemanticDiagnostic::Warning;
    SemanticIndex::getInstance()->setSnapshot(
        snapshotFromSymbols(
            symbols,
            relationships,
            QList<SemanticDiagnostic>{insightDiagnostic},
            fileContents));

    expectBool("RTL insights panel exists", rtlInsightsTree(window) != nullptr, true);
    if (!window.semanticDocks || !window.semanticDocks->rtlInsightsPanelCoordinator())
        return;

    window.semanticDocks->rtlInsightsPanelCoordinator()->showModuleInsights(
        fixturePath,
        QStringLiteral("insight_top"),
        QStringLiteral("data_q"));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    bool sawPort = false;
    bool sawClock = false;
    bool sawRelationshipEvidence = false;
    bool sawRelationshipFromEndpoint = false;
    bool sawRelationshipToEndpoint = false;
    bool sawContextKind = false;
    bool sawContextType = false;
    bool sawContextSourceRole = false;
    bool sawPackageMemberContext = false;
    bool sawPackageMemberKind = false;
    bool sawPackageMemberType = false;
    bool sawModuleBriefDiagnostic = false;
    bool sawModuleBriefDiagnosticSourceRole = false;
    bool sawClockSignalEndpoint = false;
    bool sawClockModuleEndpoint = false;
    bool sawClockRelationshipType = false;
    bool sawClockCategory = false;
    bool sawClockEvidenceReason = false;
    bool sawClockSourceRole = false;
    bool sawClockDomainMemberType = false;
    bool sawClockDomainMemberSourceRole = false;
    bool sawClockDomainMemberSignal = false;
    bool sawResetDomainMemberType = false;
    bool sawResetDomainMemberSignal = false;
    bool sawUnmappedClock = false;
    bool sawUnmappedClockCategory = false;
    bool sawTransition = false;
    bool sawFsmStateType = false;
    bool sawFsmStateSourceRole = false;
    bool sawFsmStateModule = false;
    bool sawFsmRegisterType = false;
    bool sawFsmRegisterSourceRole = false;
    bool sawFsmNextStateSignal = false;
    bool sawFsmNextStateSourceRole = false;
    bool sawFsmFromStateEndpoint = false;
    bool sawFsmToStateEndpoint = false;
    bool sawFsmTransitionSourceRole = false;
    bool sawSignalJourney = false;
    bool sawSignalJourneyDeclarationSourceRole = false;
    bool sawSignalJourneyFromEndpoint = false;
    bool sawSignalJourneyToEndpoint = false;
    bool sawSignalJourneyFromEndpointType = false;
    bool sawSignalJourneyFromEndpointSourceRole = false;
    bool sawSignalJourneyToEndpointType = false;
    bool sawSignalJourneyToEndpointSourceRole = false;
    bool sawSignalJourneyInterfaceConnection = false;
    bool sawSignalJourneyInterfaceKind = false;
    bool sawSignalJourneyInterfaceBase = false;
    bool sawSignalJourneyInterfacePeerType = false;
    bool sawSignalJourneyInterfaceSourceRole = false;
    const QList<QTreeWidgetItem*> items = navigableItems(rtlInsightsTree(window));
    for (QTreeWidgetItem* item : items) {
        sawPort = sawPort
            || (item->text(0) == QStringLiteral("Port")
                && item->text(1) == QStringLiteral("clk"));
        sawClock = sawClock
            || (item->text(0) == QStringLiteral("Clock")
                && item->text(1) == QStringLiteral("clk"));
        sawRelationshipEvidence = sawRelationshipEvidence
            || (item->text(0) == QStringLiteral("Outgoing")
                && item->text(1) == QStringLiteral("u_stage")
                && item->text(2) == QStringLiteral("Outgoing Instantiates"));
        sawRelationshipFromEndpoint = sawRelationshipFromEndpoint
            || (item->text(0) == QStringLiteral("From")
                && item->text(1) == QStringLiteral("insight_top")
                && item->text(2) == QStringLiteral("Instantiates"));
        sawRelationshipToEndpoint = sawRelationshipToEndpoint
            || (item->text(0) == QStringLiteral("To")
                && item->text(1) == QStringLiteral("u_stage")
                && item->text(2) == QStringLiteral("Instantiates"));
        sawContextKind = sawContextKind
            || (item->text(0) == QStringLiteral("Kind")
                && item->text(1) == QStringLiteral("package import")
                && item->text(2) == QStringLiteral("Package"));
        sawContextType = sawContextType
            || (item->text(0) == QStringLiteral("Type")
                && item->text(1) == QStringLiteral("package")
                && item->text(2) == QStringLiteral("package import"));
        sawContextSourceRole = sawContextSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("Package"));
        sawPackageMemberContext = sawPackageMemberContext
            || (item->text(0) == QStringLiteral("Package Member")
                && item->text(1) == QStringLiteral("PKG_DEPTH")
                && item->text(2) == QStringLiteral("package parameter"));
        sawPackageMemberKind = sawPackageMemberKind
            || (item->text(0) == QStringLiteral("Kind")
                && item->text(1) == QStringLiteral("package parameter")
                && item->text(2) == QStringLiteral("Package Member"));
        sawPackageMemberType = sawPackageMemberType
            || (item->text(0) == QStringLiteral("Type")
                && item->text(1) == QStringLiteral("parameter")
                && item->text(2) == QStringLiteral("package parameter"));
        sawModuleBriefDiagnostic = sawModuleBriefDiagnostic
            || (item->text(0) == QStringLiteral("Warning")
                && item->text(1) == QStringLiteral("insight warning")
                && item->text(2) == QStringLiteral("diagnostic"));
        sawModuleBriefDiagnosticSourceRole =
            sawModuleBriefDiagnosticSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("Warning"));
        sawClockSignalEndpoint = sawClockSignalEndpoint
            || (item->text(0) == QStringLiteral("Signal")
                && item->text(1) == QStringLiteral("clk")
                && item->text(2) == QStringLiteral("Clock"));
        sawClockModuleEndpoint = sawClockModuleEndpoint
            || (item->text(0) == QStringLiteral("Module")
                && item->text(1) == QStringLiteral("insight_top")
                && item->text(2) == QStringLiteral("Clock"));
        sawClockRelationshipType = sawClockRelationshipType
            || (item->text(0) == QStringLiteral("Relationship Type")
                && item->text(1) == QStringLiteral("Clock")
                && item->text(2) == QStringLiteral("Clock"));
        sawClockCategory = sawClockCategory
            || (item->text(0) == QStringLiteral("Category")
                && item->text(1) == QStringLiteral("mapped domain")
                && item->text(2) == QStringLiteral("Clock"));
        sawClockEvidenceReason = sawClockEvidenceReason
            || (item->text(0) == QStringLiteral("Reason")
                && item->text(1) == QStringLiteral("relationship")
                && item->text(2) == QStringLiteral("clk clocks insight_top"));
        sawClockSourceRole = sawClockSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("Clock"));
        sawClockDomainMemberType = sawClockDomainMemberType
            || (item->text(0) == QStringLiteral("Relationship Type")
                && item->text(1) == QStringLiteral("Clock")
                && item->text(2) == QStringLiteral("insight_top"));
        sawClockDomainMemberSourceRole = sawClockDomainMemberSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("insight_top"));
        sawClockDomainMemberSignal = sawClockDomainMemberSignal
            || (item->text(0) == QStringLiteral("Domain Signal")
                && item->text(1) == QStringLiteral("clk")
                && item->text(2) == QStringLiteral("Clock"));
        sawResetDomainMemberType = sawResetDomainMemberType
            || (item->text(0) == QStringLiteral("Relationship Type")
                && item->text(1) == QStringLiteral("Reset")
                && item->text(2) == QStringLiteral("insight_top"));
        sawResetDomainMemberSignal = sawResetDomainMemberSignal
            || (item->text(0) == QStringLiteral("Domain Signal")
                && item->text(1) == QStringLiteral("rst_n")
                && item->text(2) == QStringLiteral("Reset"));
        sawUnmappedClock = sawUnmappedClock
            || (item->text(0) == QStringLiteral("Unmapped Clock")
                && item->text(1) == QStringLiteral("scan_clk")
                && item->text(2).contains(
                    QStringLiteral("no clock domain relationship")));
        sawUnmappedClockCategory = sawUnmappedClockCategory
            || (item->text(0) == QStringLiteral("Category")
                && item->text(1) == QStringLiteral("unmapped timing")
                && item->text(2) == QStringLiteral("Unmapped Clock"));
        sawTransition = sawTransition
            || (item->text(0) == QStringLiteral("IDLE")
                && item->text(1) == QStringLiteral("RUN"));
        sawFsmStateType = sawFsmStateType
            || (item->text(0) == QStringLiteral("Type")
                && item->text(1) == QStringLiteral("enum value")
                && item->text(2) == QStringLiteral("IDLE"));
        sawFsmStateSourceRole = sawFsmStateSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("IDLE"));
        sawFsmStateModule = sawFsmStateModule
            || (item->text(0) == QStringLiteral("Module")
                && item->text(1) == QStringLiteral("insight_top")
                && item->text(2) == QStringLiteral("IDLE"));
        sawFsmRegisterType = sawFsmRegisterType
            || (item->text(0) == QStringLiteral("Type")
                && item->text(1) == QStringLiteral("enum")
                && item->text(2) == QStringLiteral("state_q"));
        sawFsmRegisterSourceRole = sawFsmRegisterSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("state_q"));
        sawFsmNextStateSignal = sawFsmNextStateSignal
            || (item->text(0) == QStringLiteral("Next State Signal")
                && item->text(1) == QStringLiteral("state_d")
                && item->text(2) == QStringLiteral("enum"));
        sawFsmNextStateSourceRole = sawFsmNextStateSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("state_d"));
        sawFsmFromStateEndpoint = sawFsmFromStateEndpoint
            || (item->text(0) == QStringLiteral("From State")
                && item->text(1) == QStringLiteral("IDLE")
                && item->text(2) == QStringLiteral("unconditional"));
        sawFsmToStateEndpoint = sawFsmToStateEndpoint
            || (item->text(0) == QStringLiteral("To State")
                && item->text(1) == QStringLiteral("RUN")
                && item->text(2) == QStringLiteral("unconditional"));
        sawFsmTransitionSourceRole = sawFsmTransitionSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2).startsWith(QStringLiteral("line ")));
        sawSignalJourney = sawSignalJourney
            || (item->text(0) == QStringLiteral("Assignments")
                && item->text(1) == QStringLiteral("next_data"));
        sawSignalJourneyDeclarationSourceRole =
            sawSignalJourneyDeclarationSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("data_q"));
        sawSignalJourneyFromEndpoint = sawSignalJourneyFromEndpoint
            || (item->text(0) == QStringLiteral("From")
                && item->text(1) == QStringLiteral("next_data")
                && item->text(2) == QStringLiteral("Assigns To"));
        sawSignalJourneyToEndpoint = sawSignalJourneyToEndpoint
            || (item->text(0) == QStringLiteral("To")
                && item->text(1) == QStringLiteral("data_q")
                && item->text(2) == QStringLiteral("Assigns To"));
        sawSignalJourneyFromEndpointType = sawSignalJourneyFromEndpointType
            || (item->text(0) == QStringLiteral("Type")
                && item->text(1) == QStringLiteral("logic")
                && item->text(2) == QStringLiteral("next_data"));
        sawSignalJourneyFromEndpointSourceRole =
            sawSignalJourneyFromEndpointSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("next_data"));
        sawSignalJourneyToEndpointType = sawSignalJourneyToEndpointType
            || (item->text(0) == QStringLiteral("Type")
                && item->text(1) == QStringLiteral("logic")
                && item->text(2) == QStringLiteral("data_q"));
        sawSignalJourneyToEndpointSourceRole =
            sawSignalJourneyToEndpointSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("data_q"));
        sawSignalJourneyInterfaceConnection =
            sawSignalJourneyInterfaceConnection
            || (item->text(0) == QStringLiteral("Interface Connections")
                && item->text(1) == QStringLiteral("if_bus")
                && item->text(2) == QStringLiteral("interface incoming References"));
        sawSignalJourneyInterfaceKind = sawSignalJourneyInterfaceKind
            || (item->text(0) == QStringLiteral("Connection")
                && item->text(1) == QStringLiteral("interface instance")
                && item->text(2) == QStringLiteral("interface incoming References"));
        sawSignalJourneyInterfaceBase = sawSignalJourneyInterfaceBase
            || (item->text(0) == QStringLiteral("Interface")
                && item->text(1) == QStringLiteral("insight_if")
                && item->text(2) == QStringLiteral("interface instance"));
        sawSignalJourneyInterfacePeerType = sawSignalJourneyInterfacePeerType
            || (item->text(0) == QStringLiteral("Peer Type")
                && item->text(1) == QStringLiteral("instance")
                && item->text(2) == QStringLiteral("References"));
        sawSignalJourneyInterfaceSourceRole = sawSignalJourneyInterfaceSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("if_bus"));
    }

    expectBool("RTL insights renders module port", sawPort, true);
    expectBool("RTL insights renders clock domain", sawClock, true);
    expectBool("RTL insights renders relationship evidence",
               sawRelationshipEvidence,
               true);
    expectBool("RTL insights renders relationship from endpoint",
               sawRelationshipFromEndpoint,
               true);
    expectBool("RTL insights renders relationship to endpoint",
               sawRelationshipToEndpoint,
               true);
    expectBool("RTL insights renders context kind",
               sawContextKind,
               true);
    expectBool("RTL insights renders context type",
               sawContextType,
               true);
    expectBool("RTL insights renders context source role",
               sawContextSourceRole,
               true);
    expectBool("RTL insights renders package member context",
               sawPackageMemberContext,
               true);
    expectBool("RTL insights renders package member kind",
               sawPackageMemberKind,
               true);
    expectBool("RTL insights renders package member type",
               sawPackageMemberType,
               true);
    expectBool("RTL insights renders module brief diagnostic",
               sawModuleBriefDiagnostic,
               true);
    expectBool("RTL insights renders module brief diagnostic source role",
               sawModuleBriefDiagnosticSourceRole,
               true);
    expectBool("RTL insights renders clock signal endpoint",
               sawClockSignalEndpoint,
               true);
    expectBool("RTL insights renders clock module endpoint",
               sawClockModuleEndpoint,
               true);
    expectBool("RTL insights renders clock relationship type",
               sawClockRelationshipType,
               true);
    expectBool("RTL insights renders clock category",
               sawClockCategory,
               true);
    expectBool("RTL insights renders clock evidence reason",
               sawClockEvidenceReason,
               true);
    expectBool("RTL insights renders clock source role",
               sawClockSourceRole,
               true);
    expectBool("RTL insights renders clock domain member type",
               sawClockDomainMemberType,
               true);
    expectBool("RTL insights renders clock domain member source role",
               sawClockDomainMemberSourceRole,
               true);
    expectBool("RTL insights renders clock domain member signal",
               sawClockDomainMemberSignal,
               true);
    expectBool("RTL insights renders reset domain member type",
               sawResetDomainMemberType,
               true);
    expectBool("RTL insights renders reset domain member signal",
               sawResetDomainMemberSignal,
               true);
    expectBool("RTL insights renders unmapped clock", sawUnmappedClock, true);
    expectBool("RTL insights renders unmapped clock category",
               sawUnmappedClockCategory,
               true);
    expectBool("RTL insights renders FSM transition", sawTransition, true);
    expectBool("RTL insights renders FSM state type",
               sawFsmStateType,
               true);
    expectBool("RTL insights renders FSM state source role",
               sawFsmStateSourceRole,
               true);
    expectBool("RTL insights renders FSM state module",
               sawFsmStateModule,
               true);
    expectBool("RTL insights renders FSM register type",
               sawFsmRegisterType,
               true);
    expectBool("RTL insights renders FSM register source role",
               sawFsmRegisterSourceRole,
               true);
    expectBool("RTL insights renders FSM next state signal",
               sawFsmNextStateSignal,
               true);
    expectBool("RTL insights renders FSM next state source role",
               sawFsmNextStateSourceRole,
               true);
    expectBool("RTL insights renders FSM from state endpoint",
               sawFsmFromStateEndpoint,
               true);
    expectBool("RTL insights renders FSM to state endpoint",
               sawFsmToStateEndpoint,
               true);
    expectBool("RTL insights renders FSM transition source role",
               sawFsmTransitionSourceRole,
               true);
    expectBool("RTL insights renders signal journey", sawSignalJourney, true);
    expectBool("RTL insights renders signal journey declaration source role",
               sawSignalJourneyDeclarationSourceRole,
               true);
    expectBool("RTL insights renders signal journey from endpoint",
               sawSignalJourneyFromEndpoint,
               true);
    expectBool("RTL insights renders signal journey to endpoint",
               sawSignalJourneyToEndpoint,
               true);
    expectBool("RTL insights renders signal journey from endpoint type",
               sawSignalJourneyFromEndpointType,
               true);
    expectBool("RTL insights renders signal journey from endpoint source role",
               sawSignalJourneyFromEndpointSourceRole,
               true);
    expectBool("RTL insights renders signal journey to endpoint type",
               sawSignalJourneyToEndpointType,
               true);
    expectBool("RTL insights renders signal journey to endpoint source role",
               sawSignalJourneyToEndpointSourceRole,
               true);
    expectBool("RTL insights renders signal journey interface connection",
               sawSignalJourneyInterfaceConnection,
               true);
    expectBool("RTL insights renders signal journey interface kind",
               sawSignalJourneyInterfaceKind,
               true);
    expectBool("RTL insights renders signal journey interface base",
               sawSignalJourneyInterfaceBase,
               true);
    expectBool("RTL insights renders signal journey interface peer type",
               sawSignalJourneyInterfacePeerType,
               true);
    expectBool("RTL insights renders signal journey interface source role",
               sawSignalJourneyInterfaceSourceRole,
               true);

    window.semanticDocks->rtlInsightsPanelCoordinator()->showModuleInsights(
        fixturePath,
        QStringLiteral("insight_top"),
        QStringLiteral("clk"));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    bool sawTimingJourney = false;
    const QList<QTreeWidgetItem*> timingItems = navigableItems(rtlInsightsTree(window));
    for (QTreeWidgetItem* item : timingItems) {
        sawTimingJourney = sawTimingJourney
            || (item->text(0) == QStringLiteral("Timing Connections")
                && item->text(1) == QStringLiteral("insight_top")
                && item->text(2) == QStringLiteral("timing outgoing Clocks"));
    }
    expectBool("RTL insights renders timing signal journey",
               sawTimingJourney,
               true);
}

static void runRtlInsightsSemanticDiffRegression(MainWindow& window,
                                                 const QString& fixturePath)
{
    printf("\n-- RTL insights semantic diff regression --\n");

    QList<sym_list::SymbolInfo> beforeSymbols;
    beforeSymbols.append(makeGuiSmokeSymbol(
        9701,
        fixturePath,
        QStringLiteral("diff_top"),
        sym_list::sym_module,
        1));
    beforeSymbols.append(makeGuiSmokeSymbol(
        9702,
        fixturePath,
        QStringLiteral("data"),
        sym_list::sym_port_input,
        2,
        QStringLiteral("diff_top")));
    beforeSymbols.append(makeGuiSmokeSymbol(
        9703,
        fixturePath,
        QStringLiteral("stale_q"),
        sym_list::sym_logic,
        6,
        QStringLiteral("diff_top")));
    beforeSymbols.append(makeGuiSmokeSymbol(
        9704,
        fixturePath,
        QStringLiteral("u_old"),
        sym_list::sym_inst,
        10,
        QStringLiteral("diff_top")));

    QList<sym_list::SymbolInfo> afterSymbols;
    afterSymbols.append(makeGuiSmokeSymbol(
        9801,
        fixturePath,
        QStringLiteral("diff_top"),
        sym_list::sym_module,
        1));
    afterSymbols.append(makeGuiSmokeSymbol(
        9802,
        fixturePath,
        QStringLiteral("data"),
        sym_list::sym_port_output,
        2,
        QStringLiteral("diff_top")));
    afterSymbols.append(makeGuiSmokeSymbol(
        9803,
        fixturePath,
        QStringLiteral("state_q"),
        sym_list::sym_logic,
        7,
        QStringLiteral("diff_top")));
    afterSymbols.append(makeGuiSmokeSymbol(
        9804,
        fixturePath,
        QStringLiteral("u_new"),
        sym_list::sym_inst,
        10,
        QStringLiteral("diff_top")));

    SemanticRelationship beforeRelationship;
    beforeRelationship.fromId = 9701;
    beforeRelationship.toId = 9704;
    beforeRelationship.type = SymbolRelationshipEngine::INSTANTIATES;

    SemanticRelationship afterRelationship;
    afterRelationship.fromId = 9801;
    afterRelationship.toId = 9804;
    afterRelationship.type = SymbolRelationshipEngine::INSTANTIATES;

    SemanticDiagnostic beforeDiagnostic;
    beforeDiagnostic.fileName = fixturePath;
    beforeDiagnostic.line = 6;
    beforeDiagnostic.column = 3;
    beforeDiagnostic.message = QStringLiteral("old warning");
    beforeDiagnostic.severity = SemanticDiagnostic::Warning;

    SemanticDiagnostic afterDiagnostic;
    afterDiagnostic.fileName = fixturePath;
    afterDiagnostic.line = 7;
    afterDiagnostic.column = 5;
    afterDiagnostic.message = QStringLiteral("new error");
    afterDiagnostic.severity = SemanticDiagnostic::Error;

    auto beforeSnapshot = snapshotFromSymbols(
        beforeSymbols,
        QList<SemanticRelationship>{beforeRelationship},
        QList<SemanticDiagnostic>{beforeDiagnostic});
    auto afterSnapshot = snapshotFromSymbols(
        afterSymbols,
        QList<SemanticRelationship>{afterRelationship},
        QList<SemanticDiagnostic>{afterDiagnostic});

    expectBool("RTL insights panel exists for semantic diff",
               rtlInsightsTree(window) != nullptr,
               true);
    if (!window.semanticDocks || !window.semanticDocks->rtlInsightsPanelCoordinator())
        return;

    window.semanticDocks->rtlInsightsPanelCoordinator()->showSemanticDiff(
        beforeSnapshot,
        afterSnapshot,
        QStringLiteral("diff_top"),
        fixturePath,
        fixturePath);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    bool sawModifiedPort = false;
    bool sawModifiedPortBefore = false;
    bool sawModifiedPortAfter = false;
    bool sawModifiedPortSourceRole = false;
    bool sawAddedSignal = false;
    bool sawRemovedSignal = false;
    bool sawAddedRelationship = false;
    bool sawRemovedRelationship = false;
    bool sawAddedRelationshipFromEndpoint = false;
    bool sawAddedRelationshipToEndpoint = false;
    bool sawAddedRelationshipSourceRole = false;
    bool sawAddedDiagnostic = false;
    bool sawAddedDiagnosticSourceRole = false;
    bool sawRemovedDiagnostic = false;
    const QList<QTreeWidgetItem*> items = navigableItems(rtlInsightsTree(window));
    for (QTreeWidgetItem* item : items) {
        sawModifiedPort = sawModifiedPort
            || (item->text(0) == QStringLiteral("Modified Ports")
                && item->text(1) == QStringLiteral("data")
                && item->text(2).contains(QStringLiteral("input -> output"))
                && item->text(2).contains(QStringLiteral("scope diff_top")));
        sawModifiedPortBefore = sawModifiedPortBefore
            || (item->text(0) == QStringLiteral("Before")
                && item->text(1) == QStringLiteral("input")
                && item->text(2) == QStringLiteral("scope diff_top"));
        sawModifiedPortAfter = sawModifiedPortAfter
            || (item->text(0) == QStringLiteral("After")
                && item->text(1) == QStringLiteral("output")
                && item->text(2) == QStringLiteral("scope diff_top"));
        sawModifiedPortSourceRole = sawModifiedPortSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("port"));
        sawAddedSignal = sawAddedSignal
            || (item->text(0) == QStringLiteral("Added Signals")
                && item->text(1) == QStringLiteral("state_q")
                && item->text(2).contains(QStringLiteral("scope diff_top")));
        sawRemovedSignal = sawRemovedSignal
            || (item->text(0) == QStringLiteral("Removed Signals")
                && item->text(1) == QStringLiteral("stale_q")
                && item->text(2).contains(QStringLiteral("scope diff_top")));
        sawAddedRelationship = sawAddedRelationship
            || (item->text(0) == QStringLiteral("Added")
                && item->text(1) == QStringLiteral("Instantiates")
                && item->text(2) == QStringLiteral("diff_top -> u_new"));
        sawAddedRelationshipFromEndpoint = sawAddedRelationshipFromEndpoint
            || (item->text(0) == QStringLiteral("From")
                && item->text(1) == QStringLiteral("diff_top")
                && item->text(2) == QStringLiteral("Instantiates"));
        sawAddedRelationshipToEndpoint = sawAddedRelationshipToEndpoint
            || (item->text(0) == QStringLiteral("To")
                && item->text(1) == QStringLiteral("u_new")
                && item->text(2) == QStringLiteral("Instantiates"));
        sawAddedRelationshipSourceRole = sawAddedRelationshipSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("Instantiates"));
        sawRemovedRelationship = sawRemovedRelationship
            || (item->text(0) == QStringLiteral("Removed")
                && item->text(1) == QStringLiteral("Instantiates")
                && item->text(2) == QStringLiteral("diff_top -> u_old"));
        sawAddedDiagnostic = sawAddedDiagnostic
            || (item->text(0) == QStringLiteral("Added")
                && item->text(1) == QStringLiteral("new error")
                && item->text(2) == QStringLiteral("Error, design source"));
        sawAddedDiagnosticSourceRole = sawAddedDiagnosticSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("Error"));
        sawRemovedDiagnostic = sawRemovedDiagnostic
            || (item->text(0) == QStringLiteral("Removed")
                && item->text(1) == QStringLiteral("old warning")
                && item->text(2) == QStringLiteral("Warning, design source"));
    }

    expectBool("RTL insights renders modified diff port", sawModifiedPort, true);
    expectBool("RTL insights renders modified diff before",
               sawModifiedPortBefore,
               true);
    expectBool("RTL insights renders modified diff after",
               sawModifiedPortAfter,
               true);
    expectBool("RTL insights renders modified diff source role",
               sawModifiedPortSourceRole,
               true);
    expectBool("RTL insights renders added diff signal", sawAddedSignal, true);
    expectBool("RTL insights renders removed diff signal", sawRemovedSignal, true);
    expectBool("RTL insights renders added diff relationship",
               sawAddedRelationship,
               true);
    expectBool("RTL insights renders added diff from endpoint",
               sawAddedRelationshipFromEndpoint,
               true);
    expectBool("RTL insights renders added diff to endpoint",
               sawAddedRelationshipToEndpoint,
               true);
    expectBool("RTL insights renders added diff relationship source role",
               sawAddedRelationshipSourceRole,
               true);
    expectBool("RTL insights renders removed diff relationship",
               sawRemovedRelationship,
               true);
    expectBool("RTL insights renders added diff diagnostic", sawAddedDiagnostic, true);
    expectBool("RTL insights renders added diff diagnostic source role",
               sawAddedDiagnosticSourceRole,
               true);
    expectBool("RTL insights renders removed diff diagnostic", sawRemovedDiagnostic, true);
}

static void runNavigationHierarchyModelRegression()
{
    printf("\n-- navigation hierarchy model regression --\n");

    NavigationWidget widget;
    widget.setActiveTab(NavigationWidget::ModuleTab);

    ModuleHierarchyGroup fileGroup;
    fileGroup.rootKind = ModuleHierarchyRootKind::FileGroup;
    fileGroup.rootName = QStringLiteral("C:/fixture/relationship_top.sv");
    fileGroup.rootDisplayName = QStringLiteral("relationship_top.sv");
    fileGroup.rootToolTip = fileGroup.rootName;
    fileGroup.childModules = {QStringLiteral("rel_top")};

    ModuleHierarchyGroup moduleGroup;
    moduleGroup.rootKind = ModuleHierarchyRootKind::ModuleRoot;
    moduleGroup.rootName = QStringLiteral("rel_top");
    moduleGroup.rootDisplayName = QStringLiteral("rel_top");
    moduleGroup.rootToolTip = QStringLiteral("Module: rel_top");
    moduleGroup.childModules = {QStringLiteral("rel_stage")};

    widget.updateModuleHierarchy({fileGroup, moduleGroup});

    QTreeWidget* moduleTree = nullptr;
    QTreeWidgetItem* fileRoot = nullptr;
    QTreeWidgetItem* moduleRoot = nullptr;
    QTreeWidgetItem* childModule = nullptr;
    const QList<QTreeWidget*> trees = widget.findChildren<QTreeWidget*>();
    for (QTreeWidget* tree : trees) {
        childModule = findItemByText(tree, QStringLiteral("rel_stage"));
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem* top = tree->topLevelItem(i);
            if (top->text(0) == QStringLiteral("relationship_top.sv"))
                fileRoot = top;
            if (top->text(0) == QStringLiteral("rel_top"))
                moduleRoot = top;
        }
        if (fileRoot && moduleRoot && childModule) {
            moduleTree = tree;
            break;
        }
    }

    expectBool("module hierarchy tree rendered", moduleTree != nullptr, true);
    expectBool("file group root rendered", fileRoot != nullptr, true);
    expectBool("module root rendered", moduleRoot != nullptr, true);
    expectBool("module child rendered", childModule != nullptr, true);
    if (!moduleTree || !fileRoot || !moduleRoot || !childModule)
        return;

    QSignalSpy moduleClicks(&widget, &NavigationWidget::moduleDoubleClicked);
    widget.onModuleTreeDoubleClicked(fileRoot, 0);
    expectBool("file group root does not navigate", moduleClicks.count() == 0, true);

    widget.onModuleTreeDoubleClicked(moduleRoot, 0);
    expectBool("module root navigates", moduleClicks.count() == 1, true);
    expectBool("module root emits name",
               moduleClicks.takeFirst().at(0).toString() == QStringLiteral("rel_top"),
               true);

    widget.onModuleTreeDoubleClicked(childModule, 0);
    expectBool("module child navigates", moduleClicks.count() == 1, true);
    expectBool("module child emits name",
               moduleClicks.takeFirst().at(0).toString() == QStringLiteral("rel_stage"),
               true);

    widget.setActiveTab(NavigationWidget::SymbolTab);

    sym_list::SymbolInfo outlineSymbol;
    outlineSymbol.fileName = QStringLiteral("C:/fixture/relationship_top.sv");
    outlineSymbol.symbolName = QStringLiteral("rel_top");
    outlineSymbol.symbolType = sym_list::sym_module;
    outlineSymbol.startLine = 42;
    outlineSymbol.startColumn = 7;
    outlineSymbol.symbolId = 1234;

    SymbolOutlineSymbolRow outlineRow;
    outlineRow.symbolRecord = semanticSymbolRecordForSymbol(outlineSymbol);
    outlineRow.symbolStableKey = outlineRow.symbolRecord.stableKey;
    outlineRow.displayName = outlineSymbol.symbolName;
    outlineRow.typeDisplayName = QStringLiteral("Module");
    outlineRow.detailDisplayName = outlineSymbol.fileName;
    outlineRow.iconKind = SymbolOutlineIconKind::Module;

    SymbolOutlineGroup outlineGroup;
    outlineGroup.declarationKind = SymbolTaxonomy::DeclarationKind::Module;
    outlineGroup.displayName = QStringLiteral("Module");
    outlineGroup.iconKind = SymbolOutlineIconKind::Module;
    outlineGroup.symbolRows = {outlineRow};
    widget.updateSymbolHierarchy({outlineGroup});

    QTreeWidgetItem* symbolItem = findItemByText(widget.symbolTreeWidget,
                                                 QStringLiteral("rel_top"));

    bool symbolClicked = false;
    SymbolOutlineSymbolRow clickedRow;
    QObject::connect(&widget, &NavigationWidget::symbolRowDoubleClicked,
                     &widget, [&](const SymbolOutlineSymbolRow& row) {
                         symbolClicked = true;
                         clickedRow = row;
                     });

    expectBool("symbol outline item rendered", symbolItem != nullptr, true);
    if (symbolItem)
        widget.onSymbolTreeDoubleClicked(symbolItem, 0);
    expectBool("symbol outline emits payload", symbolClicked, true);
    expectBool("symbol outline preserves file",
               clickedRow.symbolRecord.location.fileName == outlineSymbol.fileName,
               true);
    expectBool("symbol outline preserves location",
               clickedRow.symbolRecord.location.startLine == outlineSymbol.startLine
                   && clickedRow.symbolRecord.location.startColumn == outlineSymbol.startColumn,
               true);
    expectBool("symbol outline preserves id",
               clickedRow.symbolRecord.localHandle == outlineSymbol.symbolId,
               true);
    expectBool("symbol outline exposes stable key",
               clickedRow.symbolRecord.stableKey.isValid(),
               true);
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    runNavigationHierarchyModelRegression();

    const QString workspacePath = (argc > 1)
        ? QString::fromLocal8Bit(argv[1])
        : QDir::current().absoluteFilePath(QStringLiteral("test_sv/new"));
    const QString symbolFixturePath = (argc > 2)
        ? QString::fromLocal8Bit(argv[2])
        : QDir::current().absoluteFilePath(QStringLiteral("test_sv/test_symbols.sv"));
    const QString normalizedSymbolFixturePath =
        QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(symbolFixturePath).absoluteFilePath()));

    expectBool("workspace fixture exists", QFileInfo(workspacePath).isDir(), true);
    expectBool("symbol fixture exists", QFileInfo(symbolFixturePath).isFile(), true);

    MainWindow window;
    bool workspaceSymbolsDone = false;
    QObject::connect(window.analysisScheduler.get(),
                     &AnalysisScheduler::workspaceSymbolAnalysisFinished,
                     &window,
                     [&](const ProjectSnapshot&, int filesAnalyzed, int totalSymbols) {
                         Q_UNUSED(filesAnalyzed)
                         Q_UNUSED(totalSymbols)
                         workspaceSymbolsDone = true;
                     });

    window.resize(1100, 760);
    window.show();
    expectBool("main window visible", waitUntil([&]() { return window.isVisible(); }, 2000), true);
    QAction* newFileAction = window.findChild<QAction*>(QStringLiteral("new_file"));
    const int editorCountBeforeNewAction = window.tabManager->editorCount();
    if (newFileAction) {
        newFileAction->trigger();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    expectBool("file action routes through coordinator",
               newFileAction
                   && window.tabManager->editorCount()
                       == editorCountBeforeNewAction + 1,
               true);
    QTemporaryDir saveDir;
    expectBool("save temp dir valid", saveDir.isValid(), true);
    MyCodeEditor* saveEditor = window.tabManager->getCurrentEditor();
    expectBool("save editor exists", saveEditor != nullptr, true);
    if (saveDir.isValid() && saveEditor) {
        const QString savePath = saveDir.filePath(QStringLiteral("saved_tab.sv"));
        QFile seedFile(savePath);
        expectBool("save target seed opens",
                   seedFile.open(QIODevice::WriteOnly | QFile::Text),
                   true);
        seedFile.close();

        const QString savedText =
            QStringLiteral("module saved_tab;\nendmodule\n");
        DocumentModel* saveDocuments = window.tabManager->getDocumentModel();
        if (saveDocuments)
            saveDocuments->setDocumentFileName(saveEditor, savePath);
        saveEditor->setPlainText(savedText);
        expectBool("document model caches save editor text",
                   saveDocuments
                       && saveDocuments->documentTextForEditor(saveEditor) == savedText,
                   true);
        QSignalSpy fileSavedSpy(window.tabManager.get(), &TabManager::fileSaved);
        QSignalSpy documentSavedSpy(
            saveDocuments,
            &DocumentModel::documentSaved);
        expectBool("tab manager saves current tab",
                   window.tabManager->saveCurrentTab(),
                   true);
        QFile savedFile(savePath);
        expectBool("saved file reopens",
                   savedFile.open(QIODevice::ReadOnly | QFile::Text),
                   true);
        const QString savedFileText = QTextStream(&savedFile).readAll();
        savedFile.close();
        expectBool("tab manager writes editor text",
                   savedFileText == savedText,
                   true);
        expectBool("tab manager marks document saved",
                   saveDocuments
                       && saveDocuments->documentForEditor(saveEditor).saved,
                   true);
        expectBool("tab manager emits fileSaved",
                   fileSavedSpy.count() == 1,
                   true);
        expectBool("document model emits one saved snapshot",
                   documentSavedSpy.count() == 1,
                   true);
        const DocumentSnapshot savedDoc =
            window.tabManager->getDocumentModel()
                ? window.tabManager->getDocumentModel()->documentForEditor(saveEditor)
                : DocumentSnapshot();
        expectBool("document model marks saved tab clean",
                   savedDoc.saved && !savedDoc.dirty,
                   true);
        expectBool("document model records saved version",
                   savedDoc.savedTextVersion == savedDoc.textVersion,
                   true);
    }

    const bool workspaceOpened = window.workspaceManager->openWorkspace(workspacePath);
    expectBool("open workspace", workspaceOpened, true);
    const QStringList svFiles = window.workspaceManager->getSystemVerilogFiles();
    expectBool("workspace has SystemVerilog files", !svFiles.isEmpty(), true);
    const ProjectSnapshot project = window.workspaceManager->projectSnapshot();
    const QString normalizedWorkspacePath =
        QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(workspacePath).absoluteFilePath()));
    expectBool("project model tracks workspace root",
               project.workspaceRoot == normalizedWorkspacePath,
               true);
    expectBool("project model tracks SV files",
               project.systemVerilogFiles.size() == svFiles.size(),
               true);
    expectBool("project model has default include root",
               project.includeDirs.contains(normalizedWorkspacePath),
               true);
    const QString resolvedWorkspaceInclude =
        window.workspaceManager->resolveIncludePath(QFileInfo(svFiles.first()).fileName());
    expectBool("workspace manager resolves include by basename",
               QFileInfo(resolvedWorkspaceInclude).fileName() == QFileInfo(svFiles.first()).fileName(),
               true);
    const QString resolvedCurrentFileInclude =
        window.workspaceManager->resolveIncludePath(QFileInfo(svFiles.first()).fileName(),
                                                    svFiles.first());
    expectBool("workspace manager resolves include from current file",
               QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(resolvedCurrentFileInclude).absoluteFilePath()))
                   == QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(svFiles.first()).absoluteFilePath())),
               true);
    const QString includeSourcePath =
        QDir(workspacePath).absoluteFilePath(QStringLiteral("_svh.svh"));
    const QString includeTargetPath =
        QDir(workspacePath).absoluteFilePath(QStringLiteral("SVH_interface.sv"));
    expectBool("include source fixture exists",
               QFileInfo(includeSourcePath).isFile(),
               true);
    expectBool("include target fixture exists",
               QFileInfo(includeTargetPath).isFile(),
               true);
    const int editorCountBeforeIncludeClick = window.tabManager->editorCount();
    if (QFileInfo(includeSourcePath).isFile()
        && QFileInfo(includeTargetPath).isFile()
        && window.tabManager->openFileInTab(includeSourcePath)) {
        MyCodeEditor* includeEditor = window.tabManager->getCurrentEditor();
        expectBool("include source editor opens", includeEditor != nullptr, true);
        if (includeEditor) {
            QTextBlock includeBlock =
                findBlockContaining(includeEditor->document(),
                                    QStringLiteral("SVH_interface.sv"));
            expectBool("include directive block found",
                       includeBlock.isValid(),
                       true);
            if (includeBlock.isValid()) {
                const int includeClickPosition =
                    includeBlock.position()
                    + includeBlock.text().indexOf(QStringLiteral("SVH_interface"));
                QTextCursor includeCursor(includeEditor->document());
                includeCursor.setPosition(includeClickPosition);
                includeEditor->setTextCursor(includeCursor);
                includeEditor->centerCursor();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                const QPoint includeClickPoint =
                    includeEditor->cursorRect(includeCursor).center();
                QTest::mouseClick(includeEditor->viewport(),
                                  Qt::LeftButton,
                                  Qt::ControlModifier,
                                  includeClickPoint);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                MyCodeEditor* openedIncludeEditor =
                    window.tabManager->getCurrentEditor();
                const DocumentSnapshot openedIncludeDocument =
                    window.tabManager->getDocumentForEditor(openedIncludeEditor);
                expectBool("Ctrl+Click include opens target",
                           openedIncludeEditor
                               && QDir::cleanPath(QDir::fromNativeSeparators(
                                      QFileInfo(openedIncludeDocument.fileName)
                                          .absoluteFilePath()))
                                      == QDir::cleanPath(QDir::fromNativeSeparators(
                                             QFileInfo(includeTargetPath)
                                                 .absoluteFilePath()))
                               && window.tabManager->editorCount()
                                      == editorCountBeforeIncludeClick + 2,
                           true);
            }
        }
    }

    expectBool("workspace symbol analysis completes",
               waitUntil([&]() { return workspaceSymbolsDone; }, 60000), true);

    const QString largeFile = largestFile(svFiles);
    expectBool("large file selected", QFileInfo(largeFile).size() > 20000, true);
    expectBool("open large file", window.tabManager->openFileInTab(largeFile), true);

    MyCodeEditor* largeEditor = window.tabManager->getCurrentEditor();
    expectBool("large editor exists", largeEditor != nullptr, true);
    if (largeEditor) {
        DocumentModel* documents = window.tabManager->getDocumentModel();
        expectBool("document model exists", documents != nullptr, true);
        const DocumentSnapshot beforeEditDoc = documents
            ? documents->documentForFile(largeFile)
            : DocumentSnapshot();
        expectBool("document model tracks large file",
                   beforeEditDoc.fileName == QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(largeFile).absoluteFilePath())),
                   true);
        expectBool("tab manager exposes active document snapshot",
                   window.tabManager->getCurrentDocument().documentId
                       == beforeEditDoc.documentId,
                   true);
        expectBool("opened document starts saved", beforeEditDoc.saved, true);
        expectBool("opened document saved version matches text version",
                   beforeEditDoc.savedTextVersion == beforeEditDoc.textVersion,
                   true);
        expectBool("document model caches opened text",
                   documents
                       && documents->documentTextForFile(largeFile)
                           == largeEditor->toPlainText(),
                   true);
        expectBool("tab manager reads model open-file text",
                   window.tabManager->getPlainTextFromOpenFile(largeFile)
                       == largeEditor->toPlainText(),
                   true);
        expectBool("tab manager reads model current-tab text",
                   window.tabManager->getPlainTextFromCurrentTab()
                       == largeEditor->toPlainText(),
                   true);

        largeEditor->setFocus();
        QTextCursor cursor = largeEditor->textCursor();
        cursor.movePosition(QTextCursor::Start);
        largeEditor->setTextCursor(cursor);
        const int beforeLength = largeEditor->toPlainText().size();

        QTest::keyClick(largeEditor, Qt::Key_Return);
        QTest::keyClicks(largeEditor, "x");
        QTest::keyClick(largeEditor, Qt::Key_Down);
        QTest::keyClick(largeEditor, Qt::Key_Up);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        expectBool("large file edit applied",
                   largeEditor->toPlainText().size() >= beforeLength + 2, true);
        const DocumentSnapshot afterEditDoc = documents
            ? documents->documentForEditor(largeEditor)
            : DocumentSnapshot();
        expectBool("document model marks edit dirty", afterEditDoc.dirty, true);
        expectBool("document model increments version",
                   afterEditDoc.textVersion > beforeEditDoc.textVersion, true);
        expectBool("document model keeps saved version across edit",
                   afterEditDoc.savedTextVersion == beforeEditDoc.savedTextVersion
                       && afterEditDoc.savedTextVersion < afterEditDoc.textVersion,
                   true);
        expectBool("document model tracks cursor line", afterEditDoc.cursorLine > 0, true);
        expectBool("tab manager active snapshot tracks edit",
                   window.tabManager->getCurrentDocument().textVersion
                       == afterEditDoc.textVersion
                       && window.tabManager->getCurrentDocument().dirty,
                   true);
        expectBool("document model updates cached text",
                   documents
                       && documents->documentTextForFile(largeFile)
                           == largeEditor->toPlainText(),
                   true);
        expectBool("tab manager current text follows model cache",
                   window.tabManager->getPlainTextFromCurrentTab()
                       == largeEditor->toPlainText(),
                   true);
        expectBool("document model owns editor snapshot state",
                   documents
                       && afterEditDoc.fileName
                           == QDir::cleanPath(QDir::fromNativeSeparators(
                                  QFileInfo(largeFile).absoluteFilePath()))
                       && documents->documentText(afterEditDoc.documentId)
                              == largeEditor->toPlainText()
                       && !afterEditDoc.saved
                       && afterEditDoc.cursorLine > 0,
                   true);
        drainRelationshipWork(window);
    }

    QTemporaryDir identityDir;
    expectBool("document identity temp dir valid", identityDir.isValid(), true);
    if (identityDir.isValid()) {
        QDir identityRoot(identityDir.path());
        expectBool("document identity dir A created",
                   identityRoot.mkpath(QStringLiteral("a")),
                   true);
        expectBool("document identity dir B created",
                   identityRoot.mkpath(QStringLiteral("b")),
                   true);
        const QString sameNameA =
            identityRoot.filePath(QStringLiteral("a/same_name.sv"));
        const QString sameNameB =
            identityRoot.filePath(QStringLiteral("b/same_name.sv"));
        QFile sameFileA(sameNameA);
        expectBool("same-name file A writable",
                   sameFileA.open(QIODevice::WriteOnly | QIODevice::Text),
                   true);
        if (sameFileA.isOpen()) {
            sameFileA.write("module same_name_a; logic from_a; endmodule\n");
            sameFileA.close();
        }
        QFile sameFileB(sameNameB);
        expectBool("same-name file B writable",
                   sameFileB.open(QIODevice::WriteOnly | QIODevice::Text),
                   true);
        if (sameFileB.isOpen()) {
            sameFileB.write("module same_name_b; logic from_b; endmodule\n");
            sameFileB.close();
        }

        expectBool("open same-name file A",
                   window.tabManager->openFileInTab(sameNameA),
                   true);
        MyCodeEditor* sameEditorA = window.tabManager->getCurrentEditor();
        expectBool("same-name editor A exists", sameEditorA != nullptr, true);
        expectBool("open same-name file B",
                   window.tabManager->openFileInTab(sameNameB),
                   true);
        MyCodeEditor* sameEditorB = window.tabManager->getCurrentEditor();
        DocumentModel* documents = window.tabManager->getDocumentModel();
        expectBool("same-name editor B exists", sameEditorB != nullptr, true);
        expectBool("document model resolves native path to editor",
                   sameEditorA
                       && documents
                       && documents->editorForFile(QDir::toNativeSeparators(sameNameA))
                              == sameEditorA,
                   true);
        QSignalSpy activeDocumentSpy(window.tabManager.get(),
                                     &TabManager::activeDocumentChanged);
        expectBool("tab manager activates model-indexed file",
                   sameEditorA
                       && window.tabManager->activateOpenFile(QDir::toNativeSeparators(sameNameA))
                       && window.tabManager->getCurrentEditor() == sameEditorA,
                   true);
        expectBool("tab manager emits active document snapshot",
                   activeDocumentSpy.count() == 1
                       && activeDocumentSpy.takeFirst().at(0).value<DocumentSnapshot>().fileName
                              == QDir::cleanPath(QDir::fromNativeSeparators(
                                     QFileInfo(sameNameA).absoluteFilePath())),
                   true);
        expectBool("tab manager keeps same-name file A text distinct",
                   window.tabManager->getPlainTextFromOpenFile(sameNameA)
                       .contains(QStringLiteral("from_a")),
                   true);
        expectBool("tab manager keeps same-name file B text distinct",
                   window.tabManager->getPlainTextFromOpenFile(sameNameB)
                       .contains(QStringLiteral("from_b")),
                   true);
        expectBool("tab manager rejects basename-only open-file text lookup",
                   window.tabManager
                       ->getPlainTextFromOpenFile(QStringLiteral("same_name.sv"))
                       .isNull(),
                   true);
        drainRelationshipWork(window);
    }

    bool symbolFixtureAnalyzed = false;
    QObject::connect(window.analysisScheduler.get(), &AnalysisScheduler::fileSymbolAnalysisFinished,
                     &window, [&](const QString& fileName, int symbolsFound) {
                         if (QFileInfo(fileName).absoluteFilePath()
                             == QFileInfo(symbolFixturePath).absoluteFilePath() && symbolsFound > 0) {
                             symbolFixtureAnalyzed = true;
                         }
                     });

    expectBool("open symbol fixture", window.tabManager->openFileInTab(symbolFixturePath), true);
    expectBool("symbol fixture analysis completes",
               waitUntil([&]() { return symbolFixtureAnalyzed; }, 10000), true);

    QTemporaryDir diagnosticDir;
    expectBool("diagnostic temp dir created", diagnosticDir.isValid(), true);
    const QString diagnosticPath =
        diagnosticDir.filePath(QStringLiteral("broken_diag.sv"));
    QFile diagnosticFile(diagnosticPath);
    expectBool("diagnostic fixture writable",
               diagnosticFile.open(QIODevice::WriteOnly | QIODevice::Text), true);
    if (diagnosticFile.isOpen()) {
        diagnosticFile.write(
            "module broken_diag(input logic clk);\n"
            "  logic bad;\n"
            "  assign bad = ;\n"
            "endmodule\n");
        diagnosticFile.close();
    }
    const QString cleanDiagnosticPath =
        diagnosticDir.filePath(QStringLiteral("clean_diag.sv"));
    QFile cleanDiagnosticFile(cleanDiagnosticPath);
    expectBool("clean diagnostic fixture writable",
               cleanDiagnosticFile.open(QIODevice::WriteOnly | QIODevice::Text), true);
    if (cleanDiagnosticFile.isOpen()) {
        cleanDiagnosticFile.write(
            "module clean_diag(input logic clk, output logic done);\n"
            "  assign done = clk;\n"
            "endmodule\n");
        cleanDiagnosticFile.close();
    }

    bool diagnosticFixtureAnalyzed = false;
    QObject::connect(window.analysisScheduler.get(), &AnalysisScheduler::fileSymbolAnalysisFinished,
                     &window, [&](const QString& fileName, int) {
                         if (QFileInfo(fileName).absoluteFilePath()
                             == QFileInfo(diagnosticPath).absoluteFilePath()) {
                             diagnosticFixtureAnalyzed = true;
                         }
                     });
    expectBool("open diagnostic fixture", window.tabManager->openFileInTab(diagnosticPath), true);
    expectBool("diagnostic fixture analysis completes",
               waitUntil([&]() { return diagnosticFixtureAnalyzed; }, 10000), true);
    expectBool("diagnostic fixture snapshot updates",
               waitUntil([&]() {
                   const auto snapshot = SemanticIndex::getInstance()->snapshot();
                   return snapshot && !snapshot->getDiagnostics(diagnosticPath).isEmpty();
               }, 5000),
               true);
    expectBool("problems tree exists", problemsTree(window) != nullptr, true);
    expectBool("problems tree shows diagnostic",
               waitUntil([&]() {
                   return problemsTree(window)
                          && navigableItemCount(problemsTree(window)) > 0;
               }, 2000),
               true);
    if (problemsScopeCombo(window)) {
        problemsScopeCombo(window)->setCurrentIndex(
            problemsScopeCombo(window)->findText(QStringLiteral("All Files")));
        expectBool("problems all-files groups diagnostics",
                   waitUntil([&]() {
                       QTreeWidget* tree = problemsTree(window);
                       return tree
                              && tree->topLevelItemCount() > 0
                              && tree->topLevelItem(0)->childCount() > 0;
                   }, 2000),
                   true);
        expectBool("problems all-files group shows count",
                   problemsTree(window)
                       && problemsTree(window)->topLevelItemCount() > 0
                       && problemsTree(window)->topLevelItem(0)->text(0).contains(QStringLiteral("(")),
                   true);
        problemsScopeCombo(window)->setCurrentIndex(
            problemsScopeCombo(window)->findText(QStringLiteral("Current File")));
    }

    bool cleanDiagnosticFixtureAnalyzed = false;
    QObject::connect(window.analysisScheduler.get(), &AnalysisScheduler::fileSymbolAnalysisFinished,
                     &window, [&](const QString& fileName, int) {
                         if (QFileInfo(fileName).absoluteFilePath()
                             == QFileInfo(cleanDiagnosticPath).absoluteFilePath()) {
                             cleanDiagnosticFixtureAnalyzed = true;
                         }
                     });
    expectBool("open clean diagnostic fixture",
               window.tabManager->openFileInTab(cleanDiagnosticPath), true);
    expectBool("clean diagnostic fixture analysis completes",
               waitUntil([&]() { return cleanDiagnosticFixtureAnalyzed; }, 10000), true);
    if (problemsScopeCombo(window)) {
        problemsScopeCombo(window)->setCurrentIndex(
            problemsScopeCombo(window)->findText(QStringLiteral("All Files")));
        expectBool("problems keep previous file diagnostic",
                   waitUntil([&]() {
                       return hasNavigableFile(problemsTree(window), diagnosticPath);
                   }, 2000),
                   true);
        problemsScopeCombo(window)->setCurrentIndex(
            problemsScopeCombo(window)->findText(QStringLiteral("Workspace Files")));
        expectBool("problems workspace scope hides external diagnostic",
                   waitUntil([&]() {
                       return !hasNavigableFile(problemsTree(window), diagnosticPath);
                   }, 2000),
                   true);
        problemsScopeCombo(window)->setCurrentIndex(
            problemsScopeCombo(window)->findText(QStringLiteral("All Files")));
        expectBool("problems all-files restores external diagnostic",
                   waitUntil([&]() {
                       return hasNavigableFile(problemsTree(window), diagnosticPath);
                   }, 2000),
                   true);
        problemsScopeCombo(window)->setCurrentIndex(
            problemsScopeCombo(window)->findText(QStringLiteral("Current File")));
    }

    expectBool("reopen symbol fixture", window.tabManager->openFileInTab(symbolFixturePath), true);
    expectBool("symbol fixture analysis remains complete",
               waitUntil([&]() {
                   const auto snapshot = SemanticIndex::getInstance()->snapshot();
                   return snapshot
                       && !snapshot->getSymbolRecords(symbolFixturePath).isEmpty();
               }, 10000),
               true);

    MyCodeEditor* editor = window.tabManager->getCurrentEditor();
    expectBool("symbol editor exists", editor != nullptr, true);

    if (editor) {
        editor->setFocus();

        QTextBlock assignBlock = findBlockContaining(editor->document(), QStringLiteral("assign data_out"));
        expectBool("found insertion block", assignBlock.isValid(), true);
        if (assignBlock.isValid()) {
            QTextCursor cursor(editor->document());
            cursor.setPosition(assignBlock.position() + assignBlock.text().size());
            editor->setTextCursor(cursor);
            QTest::keyClick(editor, Qt::Key_Return);
            QTest::keyClicks(editor, "co");
        }

        QCompleter* completer = editor->findChild<QCompleter*>();
        expectBool("completion object exists", completer != nullptr, true);
        expectBool("completion popup/model becomes usable",
                   waitUntil([&]() {
                       return completer && completer->model() && completer->model()->rowCount() > 0;
                   }, 3000),
                   true);
        if (completer)
            completer->popup()->hide();
        drainRelationshipWork(window);

        QTextBlock jumpBlock = findBlockContaining(editor->document(),
                                                   QStringLiteral("counter       <= add_one(counter)"));
        expectBool("found Ctrl+Click source", jumpBlock.isValid(), true);
        if (jumpBlock.isValid()) {
            const int clickPosition = jumpBlock.position() + jumpBlock.text().indexOf(QStringLiteral("counter")) + 3;
            QTextCursor cursor(editor->document());
            cursor.setPosition(clickPosition);
            editor->setTextCursor(cursor);
            editor->centerCursor();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            const QPoint clickPoint = editor->cursorRect(cursor).center();
            QTest::mouseClick(editor->viewport(), Qt::LeftButton, Qt::ControlModifier, clickPoint);

            expectBool("Ctrl+Click jumps to counter definition",
                       waitUntil([&]() { return editor->textCursor().blockNumber() == 78; }, 2000),
                       true);
        }
    }

    NavigationWidget* navWidget = window.findChild<NavigationWidget*>();
    expectBool("navigation widget exists", navWidget != nullptr, true);
    if (navWidget && editor) {
        navWidget->setActiveTab(NavigationWidget::ModuleTab);
        window.navigationManager->setActiveView(NavigationManager::ModuleHierarchyView);
        window.navigationManager->refreshCurrentView();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QSignalSpy analysisNavigationRefreshSpy(
            window.navigationManager.get(),
            &NavigationManager::dataRefreshed);
        window.analysisScheduler->fileSymbolAnalysisFinished(
            normalizedSymbolFixturePath, 0);
        expectBool("analysis routes navigation refresh",
                   waitUntil([&]() { return analysisNavigationRefreshSpy.count() > 0; }, 1000),
                   true);
        analysisNavigationRefreshSpy.clear();
        window.analysisScheduler->workspaceSymbolAnalysisFinished(ProjectSnapshot(), 1, 0);
        expectBool("batch analysis routes navigation refresh",
                   waitUntil([&]() { return analysisNavigationRefreshSpy.count() > 0; }, 1000),
                   true);

        QTreeWidget* moduleTree = nullptr;
        QTreeWidgetItem* moduleItem = nullptr;
        const QList<QTreeWidget*> trees = navWidget->findChildren<QTreeWidget*>();
        for (QTreeWidget* tree : trees) {
            if (!tree->isVisible())
                continue;
            if (QTreeWidgetItem* item = findItemByText(tree, QStringLiteral("adder"))) {
                moduleTree = tree;
                moduleItem = item;
                break;
            }
        }

        expectBool("navigation module item exists", moduleTree && moduleItem, true);
        if (moduleTree && moduleItem) {
            moduleTree->expandAll();
            moduleTree->scrollToItem(moduleItem);
            moduleTree->setCurrentItem(moduleItem);
            moduleTree->setFocus();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            const QRect rect = moduleTree->visualItemRect(moduleItem);
            expectBool("navigation item has visual rect", rect.isValid(), true);
            bool moduleDoubleClicked = false;
            QObject::connect(navWidget, &NavigationWidget::moduleDoubleClicked,
                             &window, [&](const QString& moduleName) {
                                 if (moduleName == QStringLiteral("adder"))
                                     moduleDoubleClicked = true;
                             });
            QTest::mouseClick(moduleTree->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
            QTest::mouseDClick(moduleTree->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            expectBool("navigation double-click signal emitted", moduleDoubleClicked, true);

            expectBool("navigation double-click jumps to module",
                       waitUntil([&]() {
                           MyCodeEditor* current = window.tabManager->getCurrentEditor();
                           return current
                                  && window.tabManager
                                         ->getDocumentForEditor(current)
                                         .fileName == normalizedSymbolFixturePath
                                  && current->textCursor().blockNumber() == 51;
                       }, 2000),
                       true);
        }
    }

    runReferenceDockRegression(window, normalizedSymbolFixturePath);
    runRtlInsightsPanelRegression(window, normalizedSymbolFixturePath);
    runRtlInsightsSemanticDiffRegression(window, normalizedSymbolFixturePath);

    drainRelationshipWork(window);

    if (problemsScopeCombo(window)) {
        SemanticDiagnostic closeDiagnostic;
        closeDiagnostic.fileName = diagnosticPath;
        closeDiagnostic.line = 3;
        closeDiagnostic.column = 1;
        closeDiagnostic.message = QStringLiteral("workspace close probe");
        closeDiagnostic.severity = SemanticDiagnostic::Error;
        SemanticIndex::getInstance()->setSnapshot(
            snapshotFromSymbols(
                QList<sym_list::SymbolInfo>{},
                QList<SemanticRelationship>{},
                QList<SemanticDiagnostic>{closeDiagnostic}));
        problemsScopeCombo(window)->setCurrentIndex(
            problemsScopeCombo(window)->findText(QStringLiteral("All Files")));
        semanticPanelRefresh(window)->updateProblemsPanel();
        expectBool("problems close probe visible",
                   navigableItemCount(problemsTree(window)) == 1,
                   true);
        window.workspaceManager->closeWorkspace();
        expectBool("problems clear on workspace close",
                   waitUntil([&]() {
                       return navigableItemCount(problemsTree(window)) == 0;
                   }, 2000),
                   true);
        SemanticIndex::getInstance()->setSnapshot(
            snapshotFromSymbols(
                QList<sym_list::SymbolInfo>{},
                QList<SemanticRelationship>{},
                QList<SemanticDiagnostic>{closeDiagnostic}));
        semanticPanelRefresh(window)->updateProblemsPanel();
        expectBool("problems reopen probe visible",
                   navigableItemCount(problemsTree(window)) == 1,
                   true);
        workspaceSymbolsDone = false;
        expectBool("reopen workspace after close",
                   window.workspaceManager->openWorkspace(workspacePath), true);
        expectBool("problems preserve external diagnostic on workspace analysis start",
                   waitUntil([&]() {
                       return navigableItemCount(problemsTree(window)) == 1;
                   }, 2000),
                   true);
        expectBool("reopened workspace analysis completes",
                   waitUntil([&]() { return workspaceSymbolsDone; }, 60000),
                   true);
        expectBool("problems snapshot keeps external diagnostic after workspace analysis",
                   waitUntil([&]() {
                       const auto snapshot = SemanticIndex::getInstance()->snapshot();
                       return snapshot
                              && !snapshot->getDiagnostics(diagnosticPath).isEmpty();
                   }, 2000),
                   true);
        problemsScopeCombo(window)->setCurrentIndex(
            problemsScopeCombo(window)->findText(QStringLiteral("All Files")));
        semanticPanelRefresh(window)->updateProblemsPanel();
        expectBool("problems keep external diagnostic after workspace analysis",
                   waitUntil([&]() {
                       return hasNavigableFile(problemsTree(window), diagnosticPath);
                   }, 2000),
                   true);
        drainRelationshipWork(window);
    }

    printf("\n%d checks, %d failed\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
