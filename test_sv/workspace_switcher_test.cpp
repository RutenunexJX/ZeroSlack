#include "applicationthememanager.h"
#include "analysisscheduler.h"
#include "semanticindex.h"
#include "symbolanalyzer.h"
#include "mainwindow.h"
#include "tabmanager.h"
#include "workspacemanager.h"
#include "workspacesessioncoordinator.h"
#include "editoractioncontextservice.h"
#include "testuistyle.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

namespace {
bool write(const QString& path, const QByteArray& content) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
}
QToolButton* choice(QMenu* menu, const QString& path, const char* objectName = "workspaceChoice") {
    for (auto* button : menu->findChildren<QToolButton*>(objectName))
        if (button->property("workspacePath").toString() == path) return button;
    return nullptr;
}
}

class WorkspaceSwitcherTest final : public QObject {
    Q_OBJECT
private slots:
    void standaloneAnalysisDoesNotRecompileWorkspace() {
        QTemporaryDir files;
        QVERIFY(files.isValid());
        const QString root = files.filePath("project");
        const QString projectFile = root + "/top.sv";
        const QString temporary = files.filePath("outside/scratch.sv");
        const QByteArray projectSource =
            "module child #(parameter WIDTH = 4); logic [WIDTH-1:0] data; endmodule\n"
            "module top; child #(.WIDTH(8)) u0(); endmodule\n";
        const QByteArray temporarySource =
            "`include \"config.svh\"\n"
            "`ifdef WORKSPACE_ONLY\nmodule leaked_workspace_macro;\n"
            "`else\nmodule scratch;\n`endif\n"
            "`ifdef LOCAL_HEADER\nlogic local_header_used;\n"
            "`else\nlogic leaked_workspace_include;\n`endif\nendmodule\n";
        QVERIFY(write(projectFile, projectSource));
        QVERIFY(write(root + "/include/config.svh", "`define WORKSPACE_HEADER\n"));
        QVERIFY(write(files.filePath("outside/config.svh"), "`define LOCAL_HEADER\n"));
        QVERIFY(write(temporary, temporarySource));
        auto* index = SemanticIndex::getInstance();
        index->clearSemanticState();
        ProjectModel project;
        DocumentModel documents;
        SymbolAnalyzer analyzer;
        AnalysisScheduler scheduler;
        scheduler.setDocumentModel(&documents);
        scheduler.setSymbolAnalyzer(&analyzer);
        scheduler.setProjectModel(&project);
        QSignalSpy workspaceFinished(&scheduler, &AnalysisScheduler::workspaceSymbolAnalysisFinished);
        QSignalSpy workspaceStarted(&scheduler, &AnalysisScheduler::workspaceSymbolAnalysisStarted);
        project.setWorkspaceConfiguration({root + "/include"}, {{"WORKSPACE_ONLY", "1"}},
                                          {"sv", "svh", "v"}, "top", {});
        project.setWorkspaceState(root, {projectFile});
        QTRY_VERIFY_WITH_TIMEOUT(!workspaceFinished.isEmpty(), 15000);
        QVERIFY(!index->getSymbolRecords(projectFile).isEmpty());
        const auto workspaceRecords = index->getSymbolRecords(projectFile);
        const int startedCount = workspaceStarted.count();

        MyCodeEditor editor;
        editor.setProperty("standaloneDocument", true);
        editor.setPlainText(QString::fromUtf8(temporarySource));
        documents.registerEditor(&editor, temporary);
        QTRY_COMPARE_WITH_TIMEOUT(scheduler.semanticStatus(temporary).state,
                                  DocumentSemanticState::Current, 15000);
        QCOMPARE(workspaceStarted.count(), startedCount);
        QCOMPARE(project.systemVerilogFiles(), QStringList{projectFile});
        QCOMPARE(index->getCachedFileContent(projectFile), QString::fromUtf8(projectSource));
        QCOMPARE(index->getSymbolRecords(projectFile).size(), workspaceRecords.size());
        for (const auto& record : workspaceRecords) {
            const auto after = index->getSymbolRecordByStableKey(record.stableKey);
            QCOMPARE(after.presentation.instanceInfoByPath.keys(), record.presentation.instanceInfoByPath.keys());
            QCOMPARE(after.presentation.computationRevision, record.presentation.computationRevision);
        }
        QStringList names;
        for (const auto& record : index->getSymbolRecords(temporary)) names.append(record.name);
        QVERIFY(names.contains("scratch"));
        QVERIFY(names.contains("local_header_used"));
        QVERIFY(!names.contains("leaked_workspace_macro"));
        QVERIFY(!names.contains("leaked_workspace_include"));
        const QString otherRoot = files.filePath("other-project");
        const QString otherFile = otherRoot + "/other.sv";
        QVERIFY(write(otherFile, "module other; endmodule\n"));
        project.setWorkspaceState(otherRoot, {otherFile});
        QTRY_VERIFY_WITH_TIMEOUT(workspaceFinished.count() >= 2, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(index->getCachedFileContent(temporary),
                                  QString::fromUtf8(temporarySource), 15000);
        QCOMPARE(scheduler.semanticStatus(temporary).state, DocumentSemanticState::Current);
        project.closeProject();
        QTRY_COMPARE_WITH_TIMEOUT(index->getCachedFileContent(temporary),
                                  QString::fromUtf8(temporarySource), 15000);
        QCOMPARE(scheduler.semanticStatus(temporary).state, DocumentSemanticState::Current);
        scheduler.shutdown();
    }

    void workspaceAndTemporaryLifetimes() {
        QTemporaryDir files;
        QVERIFY(files.isValid());
        const QString a = files.filePath("workspace-a");
        const QString b = files.filePath("workspace-b");
        const QString nested = a + "/nested";
        const QString fileA = a + "/rtl/counter.sv";
        const QString fileA2 = a + "/rtl/top.v";
        const QString fileB = b + "/rtl/top.sv";
        const QString fileNested = nested + "/nested.sv";
        const QString temporary = files.filePath("outside/scratch.sv");
        const QByteArray source = "module counter;\n" + QByteArray(100, '\n') + "endmodule\n";
        QVERIFY(write(fileA, source)); QVERIFY(write(fileA2, source));
        QVERIFY(write(fileB, source)); QVERIFY(write(fileNested, source));
        QVERIFY(write(temporary, source));

        MainWindow window;
        window.resize(1050, 700); window.show();
        auto* tabs = window.tabManager.get();
        auto* workspaces = window.workspaceManager.get();
        auto* sessions = window.findChild<WorkspaceSessionCoordinator*>();
        QVERIFY(sessions);
        workspaces->setRecentWorkspacePersistenceEnabledForTesting(false);
        tabs->setCrashRecoveryService(std::make_unique<CrashRecoveryService>(files.filePath("recovery")));
        QVERIFY(sessions->openWorkspace(a));
        QVERIFY(tabs->openFileInTab(fileA));
        auto* editorA = tabs->getCurrentEditor();
        QVERIFY(tabs->openFileInTab(fileA2));
        auto* editorA2 = tabs->getCurrentEditor();
        editorA2->moveCursor(QTextCursor::End);
        editorA2->insertPlainText("// unsaved A\n");
        QTextCursor cursor = editorA2->textCursor(); cursor.setPosition(30); editorA2->setTextCursor(cursor);
        editorA2->verticalScrollBar()->setValue(12);
        const int savedScroll = editorA2->verticalScrollBar()->value();
        const int savedCursor = editorA2->textCursor().position();
        QVERIFY(tabs->workspaceHasUnsavedChanges(a));
        QVERIFY(tabs->openFileInTab(temporary));
        auto* external = tabs->getCurrentEditor();
        external->insertPlainText("// temporary edit\n");
        QVERIFY(tabs->isTemporaryEditor(external));
        QVERIFY(external->hierarchyInstanceContext().workspacePath.isEmpty());
        QVERIFY(external->editorSemanticContextForPosition(0).standaloneDocument);
        EditorActionContextService actionContext;
        actionContext.updateWorkspaceContext(workspaces->projectSnapshot());
        EditorActionContextQuery query;
        query.editorContext = external->editorSemanticContextForPosition(0);
        const auto resolved = actionContext.resolve(query);
        QVERIFY(resolved.workspacePath.isEmpty()); QVERIFY(!resolved.hierarchyBound());
        auto* externalGroup = tabs->editorSplitController()->groupForPage(external);
        QVERIFY(externalGroup->tabText(externalGroup->indexOf(external)).contains("TEMP"));
        QVERIFY(externalGroup->tabBar()->property("temporaryTabBoundary").toInt() >= 0);
        externalGroup->tabBar()->moveTab(externalGroup->indexOf(external), 0);
        QTRY_COMPARE(externalGroup->indexOf(external), externalGroup->count() - 1);
        QCOMPARE(externalGroup->tabBar()->property("temporaryTabBoundary").toInt(),
                 externalGroup->indexOf(external));
        QCOMPARE(tabs->workspaceSessionTabs(a).size(), 2);
        QVERIFY(tabs->workspaceSessionTabs(a).at(1).active);
        tabs->checkpointCrashRecovery();
        const auto aRecovery = tabs->listCrashRecoveryCandidates(a);
        for (const auto& candidate : aRecovery.candidates) QVERIFY(candidate.originalFilePath != temporary);
        const auto tempRecovery = tabs->listCrashRecoveryCandidates(tabs->temporaryRecoveryWorkspace());
        QVERIFY(tempRecovery.succeeded()); QCOMPARE(tempRecovery.candidates.size(), 1);
        QCOMPARE(tempRecovery.candidates.first().originalFilePath, temporary);

        QVERIFY(sessions->openWorkspace(b)); QVERIFY(tabs->openFileInTab(fileB));
        auto* editorB = tabs->getCurrentEditor();
        auto* sidebar = window.findChild<QToolButton*>("sidebarWorkspaceSwitcher");
        auto* title = window.findChild<QToolButton*>("titleWorkspaceSwitcher");
        auto* popup = window.findChild<QMenu*>("workspaceSwitcherPopup");
        QVERIFY(sidebar && title && popup);
        auto* expand = window.findChild<QToolButton*>("expandProjectSidebarButton");
        if (!sidebar->isVisible() && expand) { expand->click(); QTRY_VERIFY(sidebar->isVisible()); }
        QTRY_VERIFY(!title->isVisible());
        const int count = tabs->editorCount();
        for (int i = 0; i < 3; ++i) {
            sidebar->click(); QTRY_VERIFY(popup->isVisible());
            auto* toA = choice(popup, a); QVERIFY(toA);
            QVERIFY(toA->text().isEmpty());
            QVERIFY(toA->height() < 100);
            if (i == 0) {
                QTest::keyClick(choice(popup, b), Qt::Key_Up);
                QCOMPARE(QApplication::focusWidget(), toA);
            }
            const QString evidence = qEnvironmentVariable("ZEROSLACK_UI_EVIDENCE_DIR");
            if (i == 0 && !evidence.isEmpty()) {
                QDir().mkpath(evidence);
                window.grab().save(evidence + "/workspace-window.png");
                popup->grab().save(evidence + "/workspace-popup.png");
            }
            QVERIFY(toA->findChildren<QLabel*>().first()->text().contains(QChar(0x25cf)));
            toA->click(); QTRY_COMPARE(workspaces->getWorkspacePath(), a);
            QCOMPARE(tabs->getCurrentEditor(), editorA2);
            QCOMPARE(editorA2->textCursor().position(), savedCursor);
            QCOMPARE(editorA2->verticalScrollBar()->value(), savedScroll);
            QVERIFY(editorA2->toPlainText().contains("unsaved A"));
            QVERIFY(tabs->activateOpenFile(temporary));
            QCOMPARE(workspaces->getWorkspacePath(), a);
            sidebar->click(); QTRY_VERIFY(popup->isVisible());
            auto* toB = choice(popup, b); QVERIFY(toB); toB->click();
            QTRY_COMPARE(workspaces->getWorkspacePath(), b);
            QCOMPARE(tabs->getCurrentEditor(), editorB);
            QCOMPARE(tabs->editorCount(), count);
            QVERIFY(externalGroup->isTabVisible(externalGroup->indexOf(external)));
        }
        QVERIFY(sessions->switchWorkspace(0));
        auto* collapse = window.findChild<QToolButton*>("collapseProjectSidebarButton");
        QVERIFY(collapse); collapse->click();
        QTRY_VERIFY(title->isVisible()); QTRY_VERIFY(!sidebar->isVisible());
        title->click(); QTRY_VERIFY(popup->isVisible());
        QVERIFY(choice(popup, b)); popup->hide();
        auto* project = window.findChild<QToolButton*>("projectRailButton");
        auto* settings = window.findChild<QToolButton*>("settingsRailButton");
        QVERIFY(project && settings); QVERIFY(!project->isVisible()); QVERIFY(!settings->isVisible());
        expand->click(); QTRY_VERIFY(sidebar->isVisible()); QTRY_VERIFY(!title->isVisible());

        int reviews = 0;
        bool reviewMatchesWorkspace = true;
        tabs->setTabLocked(editorA2, true);
        tabs->unsavedDocumentManagerForTesting()->setDecisionProvider(
            [&](const QList<PendingDocumentChange>& changes, QWidget*) {
                ++reviews;
                reviewMatchesWorkspace &= changes.size() == 1 && changes.first().fileName == fileA2;
                return reviews == 1 ? UnsavedDocumentBatchDecision::Cancel : UnsavedDocumentBatchDecision::DiscardAll;
            });
        sidebar->click(); QTRY_VERIFY(popup->isVisible());
        auto* closeA = choice(popup, a, "workspaceCloseButton"); QVERIFY(closeA); closeA->click();
        QTRY_COMPARE(reviews, 1);
        QCOMPARE(workspaces->workspaceEntries().size(), 2);
        QVERIFY(tabs->isTabLocked(editorA2));
        sidebar->click(); QTRY_VERIFY(popup->isVisible());
        closeA = choice(popup, a, "workspaceCloseButton"); QVERIFY(closeA); closeA->click();
        QTRY_COMPARE(workspaces->workspaceEntries().size(), 1);
        QCOMPARE(workspaces->getWorkspacePath(), b);
        QVERIFY(tabs->activateOpenFile(temporary)); QVERIFY(external->document()->isModified());
        QCOMPARE(reviews, 2);
        QVERIFY(reviewMatchesWorkspace);
        QVERIFY(tabs->saveCurrentTab());
        QFile saved(temporary); QVERIFY(saved.open(QIODevice::ReadOnly));
        QVERIFY(saved.readAll().contains("temporary edit"));
        QVERIFY(tabs->listCrashRecoveryCandidates(tabs->temporaryRecoveryWorkspace()).candidates.isEmpty());
        QVERIFY(!QFileInfo::exists(files.filePath("outside/.zeroslack")));

        // The deepest open root owns a file; closing its parent leaves it open.
        QVERIFY(sessions->openWorkspace(a)); QVERIFY(sessions->openWorkspace(nested));
        QVERIFY(tabs->openFileInTab(fileNested));
        auto* nestedEditor = tabs->getCurrentEditor();
        QCOMPARE(tabs->workspaceForFile(fileNested), nested);
        QVERIFY(sessions->closeWorkspace(1));
        QVERIFY(tabs->openEditors().contains(nestedEditor));
        Q_UNUSED(editorA);
    }
};

int main(int argc, char** argv) {
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    QTemporaryDir profile; if (!profile.isValid()) return 2;
    QCoreApplication::setApplicationName("ZeroSlack-Workspace-Switcher-Test");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, profile.path());
    qputenv("ZEROSLACK_SESSION_STORAGE_PATH", (profile.path() + "/sessions.ini").toUtf8());
    if (!initializeUiStyleForTest()) return 3;
    ApplicationThemeManager::instance().applyToApplication();
    WorkspaceSwitcherTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "workspace_switcher_test.moc"
