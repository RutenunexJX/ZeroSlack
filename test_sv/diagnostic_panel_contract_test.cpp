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
    check(service->reportForPanel(options)->visible.totalCount == 3, "AllFiles preserves file-less diagnostics");
    options.scope = DiagnosticPanelScope::WorkspaceFiles; options.workspaceFiles = {a};
    check(service->reportForPanel(options)->visible.totalCount == 1, "WorkspaceFiles retains explicit membership");
    options.workspaceFiles.clear();
    check(service->reportForPanel(options)->visible.totalCount == 0, "empty workspace is not an all-files wildcard");

    QMainWindow host; ProblemsPanelCoordinator panel(&host);
    QString current; QStringList workspace{a,b};
    panel.setCurrentFileProvider([&] { return current; });
    panel.setWorkspaceFilesProvider([&] { return workspace; });
    panel.setWorkspaceRootProvider([&] { return root; });
    const auto countIs = [&](DiagnosticSeverityFilter filter, int count) {
        return panel.severityButton(filter)->text().endsWith(" " + QString::number(count));
    };
    host.addDockWidget(Qt::BottomDockWidgetArea, panel.dock());
    host.resize(1000,500); host.show(); QApplication::processEvents(); panel.update();
    check(panel.scopeCombo()->currentData().toInt() == 0 && diagnosticRows(panel) == 0
          && panel.tree()->topLevelItemCount() == 0 && panel.emptyLabel()->text() == "No file open",
          "Current File is default and absent file is a separate empty state");
    current = a; panel.update();
    check(diagnosticRows(panel) == 1 && countIs(DiagnosticSeverityFilter::Errors,1), "current file counts and rows agree");
    auto* row = panel.tree()->topLevelItem(0);
    check(panel.tree()->columnCount() == 3 && row->text(1) == error.message && row->text(2) == "1:2"
          && row->text(0).isEmpty() && !row->icon(0).isNull()
          && row->data(0,Qt::AccessibleTextRole).toString() == "Error"
          && row->toolTip(1).contains("Slang") && !panel.tree()->rootIsDecorated(),
          "compact severity, message, location and owner hover replace repeated file and internal columns");
    current.clear(); panel.update();
    check(diagnosticRows(panel) == 0 && countIs(DiagnosticSeverityFilter::All,0)
          && panel.dock()->property("bottomBadgeText").toString().isEmpty(), "removing target clears rows, counts and badge");
    panel.scopeCombo()->setCurrentIndex(2);
    check(diagnosticRows(panel) == 3 && countIs(DiagnosticSeverityFilter::Errors,1)
          && countIs(DiagnosticSeverityFilter::Warnings,1) && countIs(DiagnosticSeverityFilter::Info,1),
          "severity counts follow selected All Files scope even without current file");
    panel.severityButton(DiagnosticSeverityFilter::Warnings)->click();
    check(diagnosticRows(panel) == 1 && countIs(DiagnosticSeverityFilter::All,3)
          && countIs(DiagnosticSeverityFilter::Errors,1), "filter retains unfiltered scope counts");
    panel.scopeCombo()->setCurrentIndex(0); current = a; panel.update();
    check(panel.tree()->topLevelItemCount() == 0 && panel.emptyLabel()->text().contains("selected filter"), "filtered empty has no synthetic diagnostic row");
    panel.severityButton(DiagnosticSeverityFilter::All)->click(); panel.scopeCombo()->setCurrentIndex(1);
    check(diagnosticRows(panel) == 2 && countIs(DiagnosticSeverityFilter::All,2), "Workspace Files excludes file-less records and counts");
    check(panel.tree()->topLevelItem(0)->text(0).startsWith("Current.sv ("), "file group title uses a workspace-relative path and count");

    QList<SemanticDiagnostic> many;
    for (int i=1; i<=80; ++i) many.append(diagnostic(a,i,SemanticDiagnostic::Error));
    many.append(warning); publish(many); panel.scopeCombo()->setCurrentIndex(2); panel.update();
    panel.tree()->expandAll(); QApplication::processEvents();
    auto* group = panel.tree()->topLevelItem(0); auto* selected = group->child(12);
    panel.tree()->setCurrentItem(selected); panel.tree()->verticalScrollBar()->setValue(40);
    const int scroll = panel.tree()->verticalScrollBar()->value();
    const QPersistentModelIndex modelIndex = panel.tree()->currentIndex();
    for (int i=0; i<20; ++i) panel.update();
    check(panel.tree()->topLevelItem(0) == group && panel.tree()->currentItem() == selected && modelIndex.isValid(), "unchanged publication retains tree identity and selection");
    check(group->isExpanded() && panel.tree()->verticalScrollBar()->value() == scroll, "unchanged publication retains reading position");
    panel.setAnalysisState("analyzing"); panel.update();
    check(panel.stateLabel()->text() == QString::fromUtf8("Analyzing…") && panel.tree()->currentItem() == selected, "analysis status updates independently of cached rows");
    panel.setAnalysisState("current"); panel.update();
    check(panel.stateLabel()->isHidden(), "current status does not occupy toolbar space");
    QTest::keyClick(panel.tree(),Qt::Key_Down); check(panel.tree()->currentItem() != selected, "keyboard selection remains usable");
    panel.tree()->setCurrentItem(selected); panel.tree()->topLevelItem(1)->setExpanded(false);
    many.last().message = "changed background"; publish(many); panel.update();
    check(diagnosticRows(panel) == 81 && panel.tree()->currentItem()->text(1) == "diagnostic 13"
          && !panel.tree()->topLevelItem(1)->isExpanded(), "new publication retains selection and collapsed groups");
    SemanticAnalysisBandMetadata currentBand{"priority","Current",true,1};
    SemanticAnalysisBandMetadata backgroundBand{"background","Background",false,1};
    index->setWorkspaceFileAnalysisBands({{a,currentBand},{b,backgroundBand}}); panel.update();
    check(diagnosticRows(panel) == 81 && !host.findChild<QComboBox*>("problemsBandCombo"), "all bands remain included without an internal band filter");
    index->setWorkspaceFileAnalysisBands({{a,backgroundBand},{b,currentBand}}); panel.update();
    check(diagnosticRows(panel) == 81 && countIs(DiagnosticSeverityFilter::Errors,80), "band publication cannot hide diagnostics or change scope totals");
    index->clearWorkspaceFileAnalysisBands(); panel.update();
    panel.scopeCombo()->setCurrentIndex(1); workspace = {b}; panel.update();
    check(diagnosticRows(panel) == 1 && countIs(DiagnosticSeverityFilter::All,1), "membership changes refresh rows and counts");
    panel.scopeCombo()->setCurrentIndex(0); current = b; panel.update();
    int navigations = 0;
    panel.setNavigationHandler([&](const QString& file,int line,int column) { ++navigations; return file == b && line == 2 && column == 2; });
    auto* target = panel.tree()->topLevelItem(0); panel.tree()->setCurrentItem(target);
    QTest::keyClick(panel.tree(),Qt::Key_Return);
    QApplication::processEvents();
    const QPoint targetCenter = panel.tree()->visualItemRect(target).center();
    QTest::mouseClick(panel.tree()->viewport(), Qt::LeftButton, Qt::NoModifier, targetCenter);
    QTest::mouseDClick(panel.tree()->viewport(), Qt::LeftButton, Qt::NoModifier, targetCenter);
    check(navigations == 2, "Enter and double click both retain exact source coordinates");
    panel.dock()->hide(); publish({error}); current = a; panel.update(); panel.dock()->show(); QApplication::processEvents();
    check(diagnosticRows(panel) == 1 && countIs(DiagnosticSeverityFilter::Errors,1), "hidden publication is current on redisplay");
    publish(many,3); panel.scopeCombo()->setCurrentIndex(2); panel.update();
    check(diagnosticRows(panel) == 3, "existing diagnostic display-limit contract remains");
    publish({}); panel.update();
    check(panel.tree()->topLevelItemCount() == 0 && countIs(DiagnosticSeverityFilter::All,0)
          && panel.emptyLabel()->text() == "No issues found", "completed empty publication has a centered no-issues state");
    panel.setAnalysisState("analyzing"); check(panel.emptyLabel()->text() == QString::fromUtf8("Analyzing…"), "empty analyzing state is distinct");
    panel.setAnalysisState("stale"); check(panel.stateLabel()->text() == "Results need update", "stale state remains relevant");
    panel.setAnalysisState("failed"); check(panel.emptyLabel()->text() == "Analysis failed", "analysis failure is not presented as no issues");
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
    check(diagnosticRows(*panel) == 0 && panel->severityButton(DiagnosticSeverityFilter::Errors)->text() == "Errors 0", "MainWindow without tabs has no CurrentFile target");
    window.tabManager->createNewTab(); panel->update();
    check(window.tabManager->getCurrentEditor() && window.tabManager->getCurrentEditor()->documentFileName().isEmpty()
          && diagnosticRows(*panel) == 0, "unnamed product document does not use all-file diagnostics");
    window.tabManager->closeAllTabs();
    check(window.tabManager->openFileInTab(file), "real TabManager opens named source");
    panel->update();
    check(diagnosticRows(*panel) == 1 && panel->severityButton(DiagnosticSeverityFilter::Errors)->text() == "Errors 1", "named product editor restores CurrentFile diagnostics");
    window.tabManager->closeTab(0); panel->update();
    check(!window.tabManager->getCurrentEditor() && diagnosticRows(*panel) == 0, "closing the last product tab removes the current target");
    panel->scopeCombo()->setCurrentIndex(2);
    check(diagnosticRows(*panel) == 2, "product AllFiles still displays file-less diagnostics after tab close");
    const QString next = QDir(root).filePath("NextWorkspace"); QDir().mkpath(next);
    check(window.workspaceManager->openWorkspace(next), "product switches to a workspace without open documents");
    publish({diagnostic(file,1,SemanticDiagnostic::Error)}); panel->scopeCombo()->setCurrentIndex(0); panel->update();
    check(diagnosticRows(*panel) == 0 && panel->severityButton(DiagnosticSeverityFilter::Errors)->text() == "Errors 0", "new workspace with no editor does not borrow previous diagnostics");
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
