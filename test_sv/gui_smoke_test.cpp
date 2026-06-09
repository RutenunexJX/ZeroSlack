// Offscreen GUI smoke test for the real MainWindow/TabManager/MyCodeEditor path.
// It keeps the assertions coarse on purpose: this target is a repeatable guard that
// the GUI workflow is alive, while detailed semantic behavior stays in the focused
// headless tests.
#include <QApplication>
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
#include <QTreeWidget>
#include <QtTest/QTest>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>

#define private public
#include "mainwindow.h"
#include "documentmodel.h"
#include "navigationwidget.h"
#include "navigationmanager.h"
#include "analysisscheduler.h"
#include "semanticindex.h"
#include "semanticindexsnapshot.h"
#include "symbolanalyzer.h"
#include "tabmanager.h"
#include "workspacemanager.h"
#undef private

static int g_checks = 0;
static int g_fails = 0;

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

static void drainRelationshipWork(MainWindow& window)
{
    if (window.relationshipBuilder)
        window.relationshipBuilder->cancelAnalysis();
    if (window.analysisScheduler) {
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
        std::make_shared<const SemanticIndexSnapshot>(
            QList<sym_list::SymbolInfo>{referenced, referencing, externalReferencing, target},
            QList<SemanticRelationship>{incomingRelationship,
                                        externalIncomingRelationship,
                                        outgoingRelationship}));

    window.showReferencesForSymbol(QStringLiteral("target_ref"),
                                   fixturePath,
                                   QStringLiteral("ref_top"));

    expectBool("references tree exists", window.referencesTree != nullptr, true);
    expectBool("reference results rendered",
               navigableItemCount(window.referencesTree) == 2,
               true);
    expectBool("reference scope filter exists",
               window.referenceScopeCombo != nullptr,
               true);
    expectBool("reference type filter exists",
               window.referenceTypeCombo != nullptr,
               true);
    if (window.referenceScopeCombo) {
        window.referenceScopeCombo->setCurrentIndex(
            window.referenceScopeCombo->findText(QStringLiteral("Workspace Files")));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        expectBool("reference workspace scope hides non-workspace files",
                   navigableItemCount(window.referencesTree) == 0,
                   true);
    }
    if (window.referenceScopeCombo) {
        window.referenceScopeCombo->setCurrentIndex(
            window.referenceScopeCombo->findText(QStringLiteral("Current File")));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        expectBool("reference scope narrows to current file",
                   navigableItemCount(window.referencesTree) == 1,
                   true);
    }
    if (window.referencesTree && navigableItemCount(window.referencesTree) == 1) {
        QTreeWidgetItem* item = firstNavigableItem(window.referencesTree);
        expectBool("reference row uses source symbol",
                   item && item->text(0) == QStringLiteral("source_ref"),
                   true);
        expectBool("reference row stores source line",
                   item && item->data(0, Qt::UserRole + 1).toInt() == referencing.startLine,
                   true);
    }
    if (window.referenceScopeCombo && window.referenceTypeCombo) {
        window.referenceScopeCombo->setCurrentIndex(
            window.referenceScopeCombo->findText(QStringLiteral("All Files")));
        window.referenceTypeCombo->setCurrentIndex(
            window.referenceTypeCombo->findText(QStringLiteral("Reads From")));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        expectBool("reference type filter narrows results",
                   navigableItemCount(window.referencesTree) == 1,
                   true);
        QTreeWidgetItem* item = firstNavigableItem(window.referencesTree);
        expectBool("reference type filter keeps external source",
                   item && item->text(0) == QStringLiteral("external_ref"),
                   true);
        window.referenceTypeCombo->setCurrentIndex(
            window.referenceTypeCombo->findText(QStringLiteral("All Types")));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }

    MyCodeEditor shortcutEditor;
    shortcutEditor.setFileName(fixturePath);
    shortcutEditor.setPlainText(
        "module ref_top;\n"
        "  logic target_ref;\n"
        "endmodule\n");
    const int targetOffset = shortcutEditor.toPlainText().indexOf(QStringLiteral("target_ref")) + 2;
    QTextCursor shortcutCursor(shortcutEditor.document());
    shortcutCursor.setPosition(targetOffset);
    shortcutEditor.setTextCursor(shortcutCursor);
    QSignalSpy referenceShortcutSpy(&shortcutEditor,
                                    &MyCodeEditor::referenceSearchRequested);
    QSignalSpy relationshipShortcutSpy(&shortcutEditor,
                                       &MyCodeEditor::relationshipBrowseRequested);
    QTest::keyClick(&shortcutEditor, Qt::Key_F12, Qt::ShiftModifier);
    expectBool("find references shortcut emits request",
               referenceShortcutSpy.count() == 1,
               true);
    if (referenceShortcutSpy.count() == 1) {
        const QList<QVariant> args = referenceShortcutSpy.takeFirst();
        expectBool("find references shortcut emits symbol",
                   args.at(0).toString() == QStringLiteral("target_ref"),
                   true);
    }
    QTest::keyClick(&shortcutEditor, Qt::Key_R,
                    Qt::ControlModifier | Qt::ShiftModifier);
    expectBool("show relationships shortcut emits request",
               relationshipShortcutSpy.count() == 1,
               true);
    if (relationshipShortcutSpy.count() == 1) {
        const QList<QVariant> args = relationshipShortcutSpy.takeFirst();
        expectBool("show relationships shortcut emits symbol",
                   args.at(0).toString() == QStringLiteral("target_ref"),
                   true);
    }

    window.showRelationshipsForSymbol(QStringLiteral("target_ref"),
                                      fixturePath,
                                      QStringLiteral("ref_top"));
    expectBool("relationships tree exists", window.relationshipsTree != nullptr, true);
    expectBool("relationship results rendered",
               navigableItemCount(window.relationshipsTree) == 3,
               true);
    if (window.relationshipsTree && navigableItemCount(window.relationshipsTree) == 3) {
        bool sawIncoming = false;
        bool sawOutgoing = false;
        bool sawExternal = false;
        const QList<QTreeWidgetItem*> items = navigableItems(window.relationshipsTree);
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
        }
        expectBool("incoming relationship row rendered", sawIncoming, true);
        expectBool("outgoing relationship row rendered", sawOutgoing, true);
        expectBool("external relationship row rendered", sawExternal, true);
    }

    expectBool("relationship direction filter exists",
               window.relationshipDirectionCombo != nullptr,
               true);
    expectBool("relationship type filter exists",
               window.relationshipTypeCombo != nullptr,
               true);
    expectBool("relationship view filter exists",
               window.relationshipViewCombo != nullptr,
               true);
    expectBool("relationship depth filter exists",
               window.relationshipDepthCombo != nullptr,
               true);
    if (window.relationshipDirectionCombo && window.relationshipTypeCombo) {
        window.relationshipDirectionCombo->setCurrentIndex(
            window.relationshipDirectionCombo->findText(QStringLiteral("Outgoing")));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        expectBool("outgoing filter narrows relationships",
                   navigableItemCount(window.relationshipsTree) == 1,
                   true);
        if (window.relationshipsTree && navigableItemCount(window.relationshipsTree) == 1) {
            QTreeWidgetItem* item = firstNavigableItem(window.relationshipsTree);
            expectBool("outgoing filter keeps target",
                       item && item->text(1) == QStringLiteral("target_sink"),
                       true);
        }

        window.relationshipDirectionCombo->setCurrentIndex(
            window.relationshipDirectionCombo->findText(QStringLiteral("All Directions")));
        window.relationshipTypeCombo->setCurrentIndex(
            window.relationshipTypeCombo->findText(QStringLiteral("References")));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        expectBool("type filter narrows relationships",
                   navigableItemCount(window.relationshipsTree) == 1,
                   true);
        if (window.relationshipsTree && navigableItemCount(window.relationshipsTree) == 1) {
            QTreeWidgetItem* item = firstNavigableItem(window.relationshipsTree);
            expectBool("type filter keeps incoming source",
                       item && item->text(1) == QStringLiteral("source_ref"),
                       true);
        }
    }
    if (window.relationshipViewCombo && window.relationshipTypeCombo
        && window.relationshipDepthCombo) {
        window.relationshipTypeCombo->setCurrentIndex(
            window.relationshipTypeCombo->findText(QStringLiteral("Calls")));
        window.relationshipDepthCombo->setCurrentIndex(
            window.relationshipDepthCombo->findText(QStringLiteral("Depth 2")));
        window.relationshipViewCombo->setCurrentIndex(
            window.relationshipViewCombo->findText(QStringLiteral("Tree")));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        expectBool("relationship tree mode renders hierarchy",
                   navigableItemCount(window.relationshipsTree) == 2,
                   true);
        bool sawTreeRoot = false;
        bool sawTreeChild = false;
        const QList<QTreeWidgetItem*> items = navigableItems(window.relationshipsTree);
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
                   window.relationshipDirectionCombo->isEnabled(),
                   true);
        window.relationshipDirectionCombo->setCurrentIndex(
            window.relationshipDirectionCombo->findText(QStringLiteral("Incoming")));
        window.relationshipTypeCombo->setCurrentIndex(
            window.relationshipTypeCombo->findText(QStringLiteral("Reads From")));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        expectBool("relationship tree incoming filter renders hierarchy",
                   navigableItemCount(window.relationshipsTree) == 2,
                   true);
        bool sawIncomingTreeSource = false;
        const QList<QTreeWidgetItem*> incomingItems = navigableItems(window.relationshipsTree);
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
            window.refreshRelationshipsPanel();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QTreeWidgetItem* refreshedRoot = nullptr;
            const QList<QTreeWidgetItem*> refreshedItems =
                navigableItems(window.relationshipsTree);
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

    SymbolOutlineGroup outlineGroup;
    outlineGroup.symbolType = sym_list::sym_module;
    outlineGroup.symbols = {outlineSymbol};
    widget.updateSymbolHierarchy({outlineGroup});

    QTreeWidgetItem* symbolItem = findItemByText(widget.symbolTreeWidget,
                                                 QStringLiteral("rel_top"));

    bool symbolClicked = false;
    sym_list::SymbolInfo clickedSymbol;
    QObject::connect(&widget, &NavigationWidget::symbolDoubleClicked,
                     &widget, [&](const sym_list::SymbolInfo& symbol) {
                         symbolClicked = true;
                         clickedSymbol = symbol;
                     });

    expectBool("symbol outline item rendered", symbolItem != nullptr, true);
    if (symbolItem)
        widget.onSymbolTreeDoubleClicked(symbolItem, 0);
    expectBool("symbol outline emits payload", symbolClicked, true);
    expectBool("symbol outline preserves file",
               clickedSymbol.fileName == outlineSymbol.fileName,
               true);
    expectBool("symbol outline preserves location",
               clickedSymbol.startLine == outlineSymbol.startLine
                   && clickedSymbol.startColumn == outlineSymbol.startColumn,
               true);
    expectBool("symbol outline preserves id",
               clickedSymbol.symbolId == outlineSymbol.symbolId,
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
    QObject::connect(window.symbolAnalyzer.get(), &SymbolAnalyzer::batchAnalysisCompleted,
                     &window, [&](int filesAnalyzed, int totalSymbols) {
                         Q_UNUSED(filesAnalyzed)
                         Q_UNUSED(totalSymbols)
                         workspaceSymbolsDone = true;
                     });

    window.resize(1100, 760);
    window.show();
    expectBool("main window visible", waitUntil([&]() { return window.isVisible(); }, 2000), true);

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
        expectBool("opened document starts saved", beforeEditDoc.saved, true);

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
        expectBool("document model tracks cursor line", afterEditDoc.cursorLine > 0, true);
        if (largeEditor->relationshipAnalysisDebounceTimer)
            largeEditor->relationshipAnalysisDebounceTimer->stop();
    }

    bool symbolFixtureAnalyzed = false;
    QObject::connect(window.symbolAnalyzer.get(), &SymbolAnalyzer::analysisCompleted,
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
    QObject::connect(window.symbolAnalyzer.get(), &SymbolAnalyzer::analysisCompleted,
                     &window, [&](const QString& fileName, int) {
                         if (QFileInfo(fileName).absoluteFilePath()
                             == QFileInfo(diagnosticPath).absoluteFilePath()) {
                             diagnosticFixtureAnalyzed = true;
                         }
                     });
    expectBool("open diagnostic fixture", window.tabManager->openFileInTab(diagnosticPath), true);
    expectBool("diagnostic fixture analysis completes",
               waitUntil([&]() { return diagnosticFixtureAnalyzed; }, 10000), true);
    expectBool("problems tree exists", window.problemsTree != nullptr, true);
    expectBool("problems tree shows diagnostic",
               waitUntil([&]() {
                   return window.problemsTree
                          && navigableItemCount(window.problemsTree) > 0;
               }, 2000),
               true);
    if (window.problemsScopeCombo) {
        window.problemsScopeCombo->setCurrentIndex(
            window.problemsScopeCombo->findText(QStringLiteral("All Files")));
        expectBool("problems all-files groups diagnostics",
                   waitUntil([&]() {
                       return window.problemsTree
                              && window.problemsTree->topLevelItemCount() > 0
                              && window.problemsTree->topLevelItem(0)->childCount() > 0;
                   }, 2000),
                   true);
        expectBool("problems all-files group shows count",
                   window.problemsTree
                       && window.problemsTree->topLevelItemCount() > 0
                       && window.problemsTree->topLevelItem(0)->text(0).contains(QStringLiteral("(")),
                   true);
        window.problemsScopeCombo->setCurrentIndex(
            window.problemsScopeCombo->findText(QStringLiteral("Current File")));
    }

    bool cleanDiagnosticFixtureAnalyzed = false;
    QObject::connect(window.symbolAnalyzer.get(), &SymbolAnalyzer::analysisCompleted,
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
    if (window.problemsScopeCombo) {
        window.problemsScopeCombo->setCurrentIndex(
            window.problemsScopeCombo->findText(QStringLiteral("All Files")));
        expectBool("problems keep previous file diagnostic",
                   waitUntil([&]() {
                       return hasNavigableFile(window.problemsTree, diagnosticPath);
                   }, 2000),
                   true);
        window.problemsScopeCombo->setCurrentIndex(
            window.problemsScopeCombo->findText(QStringLiteral("Workspace Files")));
        expectBool("problems workspace scope hides external diagnostic",
                   waitUntil([&]() {
                       return !hasNavigableFile(window.problemsTree, diagnosticPath);
                   }, 2000),
                   true);
        window.problemsScopeCombo->setCurrentIndex(
            window.problemsScopeCombo->findText(QStringLiteral("All Files")));
        expectBool("problems all-files restores external diagnostic",
                   waitUntil([&]() {
                       return hasNavigableFile(window.problemsTree, diagnosticPath);
                   }, 2000),
                   true);
        window.problemsScopeCombo->setCurrentIndex(
            window.problemsScopeCombo->findText(QStringLiteral("Current File")));
    }

    expectBool("reopen symbol fixture", window.tabManager->openFileInTab(symbolFixturePath), true);
    expectBool("symbol fixture analysis remains complete",
               waitUntil([&]() { return symbolFixtureAnalyzed; }, 10000), true);

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
        if (editor->relationshipAnalysisDebounceTimer)
            editor->relationshipAnalysisDebounceTimer->stop();

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
                           return current && current->getFileName() == normalizedSymbolFixturePath
                                  && current->textCursor().blockNumber() == 51;
                       }, 2000),
                       true);
        }
    }

    runReferenceDockRegression(window, normalizedSymbolFixturePath);

    drainRelationshipWork(window);

    if (window.problemsScopeCombo) {
        SemanticDiagnostic closeDiagnostic;
        closeDiagnostic.fileName = normalizedSymbolFixturePath;
        closeDiagnostic.line = 3;
        closeDiagnostic.column = 1;
        closeDiagnostic.message = QStringLiteral("workspace close probe");
        closeDiagnostic.severity = SemanticDiagnostic::Error;
        SemanticIndex::getInstance()->setSnapshot(
            std::make_shared<const SemanticIndexSnapshot>(
                QList<sym_list::SymbolInfo>{},
                QList<SemanticRelationship>{},
                QList<SemanticDiagnostic>{closeDiagnostic}));
        window.problemsScopeCombo->setCurrentIndex(
            window.problemsScopeCombo->findText(QStringLiteral("All Files")));
        window.updateProblemsPanel();
        expectBool("problems close probe visible",
                   navigableItemCount(window.problemsTree) == 1,
                   true);
        window.workspaceManager->closeWorkspace();
        expectBool("problems clear on workspace close",
                   waitUntil([&]() {
                       return navigableItemCount(window.problemsTree) == 0;
                   }, 2000),
                   true);
        SemanticIndex::getInstance()->setSnapshot(
            std::make_shared<const SemanticIndexSnapshot>(
                QList<sym_list::SymbolInfo>{},
                QList<SemanticRelationship>{},
                QList<SemanticDiagnostic>{closeDiagnostic}));
        window.updateProblemsPanel();
        expectBool("problems reopen probe visible",
                   navigableItemCount(window.problemsTree) == 1,
                   true);
        workspaceSymbolsDone = false;
        expectBool("reopen workspace after close",
                   window.workspaceManager->openWorkspace(workspacePath), true);
        expectBool("problems clear on workspace analysis start",
                   waitUntil([&]() {
                       return navigableItemCount(window.problemsTree) == 0;
                   }, 2000),
                   true);
        expectBool("reopened workspace analysis completes",
                   waitUntil([&]() { return workspaceSymbolsDone; }, 60000),
                   true);
        drainRelationshipWork(window);
    }

    printf("\n%d checks, %d failed\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
