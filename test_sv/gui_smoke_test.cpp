// Offscreen GUI smoke test for the real MainWindow/TabManager/MyCodeEditor path.
// It keeps the assertions coarse on purpose: this target is a repeatable guard that
// the GUI workflow is alive, while detailed semantic behavior stays in the focused
// headless tests.
#include <QApplication>
#include <QCompleter>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTextBlock>
#include <QTextCursor>
#include <QTreeWidget>
#include <QtTest/QTest>

#include <algorithm>
#include <cstdio>
#include <functional>

#define private public
#include "mainwindow.h"
#include "navigationwidget.h"
#include "navigationmanager.h"
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

static void drainRelationshipWork(MainWindow& window)
{
    if (window.relationshipBuilder)
        window.relationshipBuilder->cancelAnalysis();
    if (window.relationshipBatchWatcher && window.relationshipBatchWatcher->isRunning()) {
        QFuture<QVector<QPair<QString, QVector<RelationshipToAdd>>>> future =
            window.relationshipBatchWatcher->future();
        window.relationshipBatchWatcher->cancel();
        future.waitForFinished();
    }
    if (window.relationshipSingleFileWatcher && window.relationshipSingleFileWatcher->isRunning()) {
        QFuture<QVector<RelationshipToAdd>> future = window.relationshipSingleFileWatcher->future();
        window.relationshipSingleFileWatcher->cancel();
        future.waitForFinished();
    }
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    const QString workspacePath = (argc > 1)
        ? QString::fromLocal8Bit(argv[1])
        : QDir::current().absoluteFilePath(QStringLiteral("test_sv/new"));
    const QString symbolFixturePath = (argc > 2)
        ? QString::fromLocal8Bit(argv[2])
        : QDir::current().absoluteFilePath(QStringLiteral("test_sv/test_symbols.sv"));

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

    expectBool("workspace symbol analysis completes",
               waitUntil([&]() { return workspaceSymbolsDone; }, 60000), true);

    const QString largeFile = largestFile(svFiles);
    expectBool("large file selected", QFileInfo(largeFile).size() > 20000, true);
    expectBool("open large file", window.tabManager->openFileInTab(largeFile), true);

    MyCodeEditor* largeEditor = window.tabManager->getCurrentEditor();
    expectBool("large editor exists", largeEditor != nullptr, true);
    if (largeEditor) {
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
                           return current && current->getFileName() == symbolFixturePath
                                  && current->textCursor().blockNumber() == 51;
                       }, 2000),
                       true);
        }
    }

    drainRelationshipWork(window);

    printf("\n%d checks, %d failed\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
