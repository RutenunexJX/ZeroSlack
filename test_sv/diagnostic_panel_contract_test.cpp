#include <QtWidgets>
#include <QtTest>
#include <memory>
#include <optional>
#include <atomic>
#define private public
#include "mainwindow.h"
#undef private
#include "analysisscheduler.h"
#include "diagnosticservice.h"
#include "problemspanelcoordinator.h"
#include "semanticdockcoordinator.h"
#include "semanticindexsnapshot.h"
#include "tabmanager.h"
#include "mycodeeditor.h"
#include "workspacemanager.h"
#include "workspacesessioncoordinator.h"
#include "testuistyle.h"

namespace {
int checks = 0, failures = 0;
void check(bool ok, const char* label) {
    ++checks; failures += !ok;
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
}
bool write(const QString& path) {
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write("module sample; endmodule\n") > 0;
}
SemanticDiagnostic diagnostic(const QString& file, int line, SemanticDiagnostic::Severity severity) {
    SemanticDiagnostic value; value.fileName = file; value.line = line; value.column = 2;
    value.severity = severity; value.owner = SemanticDiagnostic::SlangCompiler;
    value.message = QString("diagnostic %1").arg(line); return value;
}
int diagnosticRows(const ProblemsPanelCoordinator& panel) {
    int count = 0;
    for (QTreeWidgetItemIterator it(panel.tree()); *it; ++it)
        if ((*it)->data(0, Qt::UserRole + 1).toInt() > 0) ++count;
    return count;
}
void publish(const QList<SemanticDiagnostic>& diagnostics, int limit = 0) {
    auto snapshot = SemanticIndexSnapshot::fromSymbolRecords({}, {}, diagnostics).withDiagnosticDisplayLimit(limit);
    SemanticIndex::getInstance()->setSnapshot(std::make_shared<SemanticIndexSnapshot>(snapshot));
}
void panelCases(const QString& root) {
    auto* index = SemanticIndex::getInstance(); index->clearSemanticState();
    auto* service = DiagnosticService::getInstance(); service->setSemanticIndex(index);
    const QString a = QDir(root).filePath("Current.sv"), b = QDir(root).filePath("Background.sv");
    check(write(a) && write(b), "diagnostic source fixtures exist");
    const auto error = diagnostic(a, 1, SemanticDiagnostic::Error);
    const auto warning = diagnostic(b, 2, SemanticDiagnostic::Warning);
    const auto global = diagnostic({}, 3, SemanticDiagnostic::Info);
    publish({error, warning, global});
    DiagnosticPanelQueryOptions options;
    check(service->findDiagnosticReport(service->queryForPanel(options)).totalCount == 0,
          "CurrentFile without a target is empty at query owner");
    check(service->findDiagnosticReport().totalCount == 3, "legacy all-files query includes file-less diagnostics");
    options.scope = DiagnosticPanelScope::AllFiles;
    check(service->reportForPanel(options)->visible.totalCount == 3, "explicit AllFiles preserves file-less diagnostics");
    options.scope = DiagnosticPanelScope::WorkspaceFiles; options.workspaceFiles = {a};
    check(service->reportForPanel(options)->visible.totalCount == 1, "WorkspaceFiles retains its explicit membership");
    options.workspaceFiles.clear();
    check(service->reportForPanel(options)->visible.totalCount == 0, "empty workspace is not an all-files wildcard");

    QMainWindow host;
    ProblemsPanelCoordinator panel(&host);
    QString current;
    QStringList workspace{a,b};
    panel.setCurrentFileProvider([&] { return current; });
    panel.setWorkspaceFilesProvider([&] { return workspace; });
    host.addDockWidget(Qt::BottomDockWidgetArea, panel.dock());
    host.resize(1000,500); host.show(); QApplication::processEvents();
    panel.update();
    check(diagnosticRows(panel) == 0 && panel.summaryLabel()->text().contains("0 errors, 0 warnings, 0 info"),
          "missing current document leaves rows and summary empty");
    current = a; panel.update();
    check(diagnosticRows(panel) == 1 && panel.summaryLabel()->text().contains("1 errors"), "named current file restores its diagnostics");
    current.clear(); panel.update();
    check(diagnosticRows(panel) == 0 && panel.dock()->property("bottomBadgeText").toString().isEmpty(), "removing current target clears count and badge");
    panel.scopeCombo()->setCurrentIndex(2);
    check(diagnosticRows(panel) == 3 && panel.summaryLabel()->text().contains("0 errors"), "AllFiles remains populated without borrowing a current summary");
    panel.scopeCombo()->setCurrentIndex(1);
    check(diagnosticRows(panel) == 2, "WorkspaceFiles excludes non-file diagnostics");

    current = a;
    QList<SemanticDiagnostic> many;
    for (int i=1; i<=80; ++i) many.append(diagnostic(a, i, SemanticDiagnostic::Error));
    many.append(warning);
    publish(many); panel.scopeCombo()->setCurrentIndex(2); panel.update();
    panel.tree()->expandAll(); QApplication::processEvents();
    auto* group = panel.tree()->topLevelItem(0);
    auto* selected = group->child(12);
    panel.tree()->setCurrentItem(selected);
    panel.tree()->verticalScrollBar()->setValue(40);
    const int scroll = panel.tree()->verticalScrollBar()->value();
    const QPersistentModelIndex modelIndex = panel.tree()->currentIndex();
    for (int i=0; i<20; ++i) panel.update();
    check(panel.tree()->topLevelItem(0) == group && panel.tree()->currentItem() == selected && modelIndex.isValid(),
          "unchanged refresh reuses tree and preserves current selection");
    check(group->isExpanded() && panel.tree()->verticalScrollBar()->value() == scroll, "unchanged refresh preserves expansion and scroll");
    panel.setAnalysisState("analyzing"); panel.update();
    check(panel.stateLabel()->text() == "Diagnostics: analyzing" && panel.tree()->currentItem() == selected,
          "external analysis state updates independently of cached rows");
    panel.setAnalysisState({}); panel.update();
    check(panel.stateLabel()->text() != "Diagnostics: analyzing", "clearing explicit state restores inferred state");
    QTest::keyClick(panel.tree(), Qt::Key_Down);
    check(panel.tree()->currentItem() != selected, "keyboard selection remains usable after unchanged refresh");
    panel.tree()->setCurrentItem(selected);
    many.last().message = "changed background";
    publish(many); panel.update();
    check(diagnosticRows(panel) == 81 && panel.tree()->currentItem()
          && panel.tree()->currentItem()->text(4) == "diagnostic 13"
          && panel.tree()->topLevelItem(0)->isExpanded(), "new publication updates data and retains surviving selection/expansion");

    SemanticAnalysisBandMetadata currentBand{"priority", "Current", true, 1};
    SemanticAnalysisBandMetadata backgroundBand{"background", "Background", false, 1};
    index->setWorkspaceFileAnalysisBands({{a,currentBand},{b,backgroundBand}});
    panel.update();
    const int backgroundIndex = panel.bandCombo()->findData("background");
    check(backgroundIndex >= 0, "band selector contains background");
    panel.bandCombo()->setCurrentIndex(backgroundIndex);
    check(diagnosticRows(panel) == 1 && panel.summaryLabel()->text().contains("80 errors"), "band filter does not filter current-file summary");
    panel.severityCombo()->setCurrentIndex(1);
    check(diagnosticRows(panel) == 0, "severity and band combine correctly");
    panel.severityCombo()->setCurrentIndex(0);
    index->setWorkspaceFileAnalysisBands({{a,backgroundBand},{b,currentBand}});
    panel.update();
    check(diagnosticRows(panel) == 80, "band-only publication invalidates the projection");
    index->clearWorkspaceFileAnalysisBands(); panel.update();
    check(diagnosticRows(panel) == 0, "cleared analysis bands invalidate the projection");
    panel.bandCombo()->setCurrentIndex(0);
    panel.scopeCombo()->setCurrentIndex(1); workspace = {b}; panel.update();
    check(diagnosticRows(panel) == 1, "workspace membership change refreshes displayed rows");
    panel.scopeCombo()->setCurrentIndex(0); current = b; panel.update();
    check(diagnosticRows(panel) == 1 && panel.summaryLabel()->text().contains("1 warnings"), "active file switch refreshes rows and summary");
    int navigations = 0;
    panel.setNavigationHandler([&](const QString& file, int line, int column) {
        ++navigations; return file == b && line == 2 && column == 2;
    });
    auto* target = panel.tree()->topLevelItem(0);
    QMetaObject::invokeMethod(panel.tree(), "itemDoubleClicked", Qt::DirectConnection,
                             Q_ARG(QTreeWidgetItem*, target), Q_ARG(int, 0));
    check(navigations == 1, "surviving diagnostic item keeps navigation payload");
    panel.dock()->hide(); publish({error}); current = a; panel.update();
    panel.dock()->show(); QApplication::processEvents();
    check(diagnosticRows(panel) == 1 && panel.summaryLabel()->text().contains("1 errors"), "hidden update and redisplay use the latest publication");
    publish(many, 3); panel.scopeCombo()->setCurrentIndex(2); panel.update();
    check(diagnosticRows(panel) == 3, "existing diagnostic display-limit contract remains in force");
    publish({}); panel.update();
    check(diagnosticRows(panel) == 0 && panel.summaryLabel()->text().contains("0 errors, 0 warnings, 0 info"), "empty publication clears every displayed count");
    index->clearSemanticState();
}

void productCases(const QString& root) {
    MainWindow window;
    window.workspaceSessionCoordinator->setRestoreOnActivation(false);
    window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
    window.analysisScheduler->shutdown();
    check(window.tabManager->closeAllTabs(), "product starts with no editor");
    const QString file = QDir(root).filePath("Product.sv");
    check(write(file) && window.workspaceManager->openWorkspace(root), "real MainWindow workspace opens");
    QElapsedTimer timer; timer.start();
    while (window.workspaceManager->isWorkspaceScanActive() && timer.elapsed() < 10000) QTest::qWait(5);
    auto* panel = window.semanticDocks->problemsPanelCoordinator();
    publish({diagnostic(file,1,SemanticDiagnostic::Error), diagnostic({},2,SemanticDiagnostic::Info)});
    panel->scopeCombo()->setCurrentIndex(0); panel->update();
    check(diagnosticRows(*panel) == 0 && panel->summaryLabel()->text().contains("0 errors"), "MainWindow without tabs has no CurrentFile target");
    window.tabManager->createNewTab(); panel->update();
    check(window.tabManager->getCurrentEditor() && window.tabManager->getCurrentEditor()->documentFileName().isEmpty()
          && diagnosticRows(*panel) == 0, "unnamed product document does not use all-file diagnostics");
    window.tabManager->closeAllTabs();
    check(window.tabManager->openFileInTab(file), "real TabManager opens named source");
    panel->update();
    check(diagnosticRows(*panel) == 1 && panel->summaryLabel()->text().contains("1 errors"), "named product editor restores CurrentFile diagnostics");
    window.tabManager->closeTab(0); panel->update();
    check(!window.tabManager->getCurrentEditor() && diagnosticRows(*panel) == 0, "closing the last product tab removes the current target");
    panel->scopeCombo()->setCurrentIndex(2);
    check(diagnosticRows(*panel) == 2, "product AllFiles still displays file-less diagnostics after tab close");
    const QString next = QDir(root).filePath("NextWorkspace"); QDir().mkpath(next);
    check(window.workspaceManager->openWorkspace(next), "product switches to a workspace without open documents");
    publish({diagnostic(file,1,SemanticDiagnostic::Error)}); panel->scopeCombo()->setCurrentIndex(0); panel->update();
    check(diagnosticRows(*panel) == 0 && panel->summaryLabel()->text().contains("0 errors"), "new workspace with no editor does not borrow previous diagnostics");
    panel->scopeCombo()->setCurrentIndex(1); panel->update();
    check(diagnosticRows(*panel) == 0, "new workspace scope excludes previous workspace files");
    window.tabManager->clearCrashRecoveryAfterNormalClose(); SemanticIndex::getInstance()->clearSemanticState();
}
}
int main(int argc, char** argv) {
    setbuf(stdout,nullptr); QTemporaryDir settings;
    QCoreApplication::setOrganizationName("ZeroSlackDiagnosticPanelContract"); QCoreApplication::setApplicationName("Isolated");
    QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    qputenv("ZEROSLACK_SESSION_STORAGE_PATH",settings.filePath("sessions.ini").toUtf8());
    QApplication app(argc,argv); app.setQuitOnLastWindowClosed(false); QStandardPaths::setTestModeEnabled(true);
    if (!initializeUiStyleForTest()) return 2;
    QTemporaryDir workspace; panelCases(workspace.path()); productCases(workspace.path());
    QThreadPool::globalInstance()->waitForDone();
    std::printf("Diagnostic panel contract: %d checks, %d failures\n", checks, failures); return failures ? 1 : 0;
}
