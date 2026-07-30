#include "mainwindow.h"
#include <QLabel>
#include "editoractioncontextservice.h"
#include "hierarchyservice.h"
#include "semanticindex.h"
#include <QDir>
#include <QFileInfo>
#include "editorcoordinator.h"
#include "exposesignaltotopdialog.h"
#include "exposesignaltotopservice.h"
#include "mycodeeditor.h"
#include "semantic_fixture_records.h"
#include "semanticindexsnapshot.h"
#include "tabmanager.h"
#include "workspacemanager.h"

#include <rtledit/edit_plan.h>

#include <QApplication>
#include <QContextMenuEvent>
#include <QDialog>
#include <QFile>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextCursor>
#include <QTimer>

#include <cstdio>
#include <memory>

namespace {
int checks = 0;
int failures = 0;

void check(const char* name, bool condition)
{
    ++checks;
    failures += condition ? 0 : 1;
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", name);
}

std::shared_ptr<const SemanticIndexSnapshot> snapshot(
    const QList<SemanticSymbolRecord>& records,
    const QHash<QString, QString>& contents = {})
{
    return std::make_shared<const SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(
            records, {}, {}, contents));
}

std::shared_ptr<const SemanticIndexSnapshot> snapshotWithRelationships(
    const QList<SemanticSymbolRecord>& records,
    const QList<SemanticRelationship>& relationships,
    const QHash<QString, QString>& contents = {})
{
    return std::make_shared<const SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(
            records, relationships, {}, contents));
}


struct MenuActionState {
    bool menuShown = false;
    bool found = false;
    bool enabled = false;
    QString visibleText;
    QString statusTip;
    QString groupTitle;
};

QAction* findMenuAction(
    QMenu* menu,
    const QString& objectName,
    QString* groupTitle = nullptr)
{
    if (!menu)
        return nullptr;
    for (QAction* action : menu->actions()) {
        if (action->objectName() == objectName) {
            if (groupTitle)
                *groupTitle = menu->title();
            return action;
        }
        if (QMenu* child = action->menu()) {
            if (QAction* found =
                    findMenuAction(child, objectName, groupTitle)) {
                return found;
            }
        }
    }
    return nullptr;
}

int positionInside(const QString& text, const QString& needle)
{
    const int start = text.indexOf(needle);
    return start < 0 ? -1 : start + qMax(0, needle.size() / 2);
}

MenuActionState contextMenuState(
    MyCodeEditor* editor,
    const QString& clickNeedle,
    const QString& oldCursorNeedle)
{
    if (!editor)
        return {};

    const QString text = editor->cachedDocumentText();
    const int oldCursorPosition =
        positionInside(text, oldCursorNeedle);
    const int clickPosition =
        positionInside(text, clickNeedle);
    if (oldCursorPosition < 0 || clickPosition < 0)
        return {};

    QTextCursor oldCursor(editor->document());
    oldCursor.setPosition(oldCursorPosition);
    editor->setTextCursor(oldCursor);

    QTextCursor clickCursor(editor->document());
    clickCursor.setPosition(clickPosition);
    const QPoint clickPoint = editor->cursorRect(clickCursor).center();

    MenuActionState state;
    const QMetaObject::Connection menuObserved = QObject::connect(
        editor,
        &MyCodeEditor::sourceSymbolContextMenuRequested,
        editor,
        [&state](QMenu* menu, const EditorSemanticContext&) {
            state.menuShown = menu != nullptr;
            if (!menu)
                return;
            QTimer::singleShot(0, menu, [&state, menu]() {
                QAction* expose = findMenuAction(
                    menu,
                    QStringLiteral("exposeSignalToTopAction"),
                    &state.groupTitle);
                state.found = expose != nullptr;
                state.enabled = expose && expose->isEnabled();
                menu->close();
                state.visibleText = expose
                    ? expose->text() : QString();
                state.statusTip = expose
                    ? expose->statusTip() : QString();
            });
        });
    QContextMenuEvent event(
        QContextMenuEvent::Mouse,
        clickPoint,
        editor->viewport()->mapToGlobal(clickPoint));
    QApplication::sendEvent(editor->viewport(), &event);
    QObject::disconnect(menuObserved);
    QCoreApplication::processEvents();
    return state;
}

ExposeSignalToTopReport readyDialogReport(
    const QString& portName)
{
    ExposeSignalToTopReport report;
    report.status = ExposeSignalToTopReportStatus::Ready;
    report.failureReason =
        rtledit::ExposeSignalFailureReason::None;
    report.sourceInstancePath =
        QStringLiteral("top.u_mid.u_leaf");
    report.targetInstancePath = QStringLiteral("top");
    report.exportedPortName = portName;
    report.affectedModules = {
        QStringLiteral("leaf"),
        QStringLiteral("mid"),
        QStringLiteral("top")};
    report.affectedFiles = {
        QStringLiteral("leaf.sv"),
        QStringLiteral("mid.sv"),
        QStringLiteral("top.sv")};
    report.affectedInstancePaths = {
        QStringLiteral("top.u_mid.u_leaf"),
        QStringLiteral("top.u_mid"),
        QStringLiteral("top")};
    ExposeSignalHierarchyStepView first;
    first.index = 0;
    first.childInstancePath =
        QStringLiteral("top.u_mid.u_leaf");
    first.childModule = QStringLiteral("leaf");
    first.childInstance = QStringLiteral("u_leaf");
    first.parentInstancePath = QStringLiteral("top.u_mid");
    first.parentModule = QStringLiteral("mid");
    report.hierarchySteps.append(first);
    report.planResult.status =
        rtledit::ExposeSignalPlanStatus::Ready;
    report.sourceDiff.status = rtledit::SourceDiffStatus::Built;
    report.renderedDiff =
        QStringLiteral("--- leaf.sv\n+++ leaf.sv\n+ output logic payload_out");
    return report;
}

void runEditorActionContextRegression()
{
    QTemporaryDir temp;
    check("action context fixture root is available", temp.isValid());
    if (!temp.isValid())
        return;

    const QString topFile =
        temp.filePath(QStringLiteral("top.sv"));
    const QString leafFile =
        temp.filePath(QStringLiteral("leaf.sv"));
    const QString topText =
        QStringLiteral("module top; leaf u_leaf(); leaf u_leaf2(); endmodule\n");
    const QString leafText =
        QStringLiteral("module leaf; logic payload; endmodule\n");
    for (const auto& source : {
             qMakePair(topFile, topText),
             qMakePair(leafFile, leafText)}) {
        QFile file(source.first);
        const bool written =
            file.open(QIODevice::WriteOnly | QIODevice::Text)
            && file.write(source.second.toUtf8())
                   == source.second.toUtf8().size();
        file.close();
        check("action context fixture source is written", written);
    }

    auto module = [](int id,
                     const QString& file,
                     const QString& text,
                     const QString& name) {
        return SemanticFixtureRecordBuilder(
                   name, SymbolTaxonomy::DeclarationKind::Module)
            .withFile(file)
            .withLocalHandle(id)
            .withLine(1)
            .withTextSpan(text.indexOf(name), name.size())
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Module)
            .record();
    };
    const SemanticSymbolRecord top =
        module(501, topFile, topText, QStringLiteral("top"));
    const SemanticSymbolRecord leaf =
        module(502, leafFile, leafText, QStringLiteral("leaf"));
    auto instance = [&](int id, const QString& name) {
        return SemanticFixtureRecordBuilder(
                   name, SymbolTaxonomy::DeclarationKind::Instance)
            .withFile(topFile)
            .withLocalHandle(id)
            .withLine(1)
            .withTextSpan(topText.indexOf(name), name.size())
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Inst)
            .withOwner(SymbolTaxonomy::SymbolOwnerScope::Module,
                       QStringLiteral("top"), top.stableKey)
            .withType(QStringLiteral("leaf"),
                      QStringLiteral("leaf"),
                      SymbolTaxonomy::DeclarationKind::Module)
            .record();
    };
    const SemanticSymbolRecord firstInstance =
        instance(503, QStringLiteral("u_leaf"));
    const SemanticSymbolRecord secondInstance =
        instance(504, QStringLiteral("u_leaf2"));

    SemanticIndex index;
    index.setSnapshot(snapshotWithRelationships(
        {top, leaf, firstInstance},
        {semanticFixtureRelationship(
            top,
            firstInstance,
            SymbolRelationshipEngine::INSTANTIATES)},
        {{topFile, topText}, {leafFile, leafText}}));
    HierarchyService hierarchy(&index);
    EditorActionContextService service(&index, &hierarchy);

    ProjectSnapshot workspaceProject;
    workspaceProject.workspaceRoot = temp.path();
    workspaceProject.topModule = QStringLiteral("top");
    workspaceProject.allFiles = {
        QDir::cleanPath(QDir::fromNativeSeparators(
            QFileInfo(topFile).absoluteFilePath())),
        QDir::cleanPath(QDir::fromNativeSeparators(
            QFileInfo(leafFile).absoluteFilePath()))};
    service.updateWorkspaceContext(workspaceProject);

    EditorActionContextQuery query;
    query.editorContext.fileName = leafFile;
    query.editorContext.moduleName = QStringLiteral("leaf");
    query.editorContext.documentRevision = 9;
    query.editorContext.hierarchyInstance = {
        temp.path(), QString(), QString()};
    query.semanticStatus.fileName = leafFile;
    query.semanticStatus.state = DocumentSemanticState::Current;
    query.semanticStatus.documentRevision = 9;

    const EditorActionContext filesContext = service.resolve(query);
    check("Files entry uniquely resolves the active hierarchy instance",
          filesContext.hierarchyCandidates.size() == 1
              && filesContext.hierarchyAutoResolved
              && filesContext.resolvedHierarchy.activeTopModule
                     == QStringLiteral("top")
              && filesContext.resolvedHierarchy.instancePath
                     == QStringLiteral("top.u_leaf"));
    check("action context exposes syntax and semantic revisions",
          filesContext.syntaxRevision == 9
              && filesContext.semanticSnapshotRevision
                     == index.snapshotRevision()
              && filesContext.semanticState
                     == EditorActionSemanticState::Current);

    query.editorContext.hierarchyInstance = {
        temp.path(),
        QStringLiteral("top"),
        QStringLiteral("top.u_leaf")};
    const EditorActionContext designContext = service.resolve(query);
    check("Files and Design entry produce the same candidate set",
          designContext.hierarchyCandidates
                  == filesContext.hierarchyCandidates
              && designContext.resolvedHierarchy
                     == filesContext.resolvedHierarchy);

    index.setSnapshot(snapshotWithRelationships(
        {top, leaf, firstInstance, secondInstance},
        {semanticFixtureRelationship(
             top,
             firstInstance,
             SymbolRelationshipEngine::INSTANTIATES),
         semanticFixtureRelationship(
             top,
             secondInstance,
             SymbolRelationshipEngine::INSTANTIATES)},
        {{topFile, topText}, {leafFile, leafText}}));
    query.editorContext.hierarchyInstance = {
        temp.path(), QString(), QString()};
    const EditorActionContext ambiguousContext = service.resolve(query);
    check("multiple hierarchy instances stay enterable and require selection",
          ambiguousContext.hierarchyCandidates.size() == 2
              && ambiguousContext.hierarchySelectionRequired
              && !ambiguousContext.hierarchyBound());
    check("multiple-instance context names each top and instance",
          ambiguousContext.hierarchyCandidates.size() == 2
              && ambiguousContext.hierarchyCandidates.at(0)
                     .displayText().contains(QStringLiteral("top"))
              && ambiguousContext.hierarchyCandidates.at(1)
                     .displayText().contains(QStringLiteral("top")));

    query.semanticStatus.state = DocumentSemanticState::Stale;
    const EditorActionContext staleContext = service.resolve(query);
    check("stale semantic state is continuously visible",
          staleContext.semanticState == EditorActionSemanticState::Stale
              && staleContext.compactText().contains(
                     QStringLiteral("stale")));
    query.semanticAnalysisActive = true;
    const EditorActionContext analyzingContext = service.resolve(query);
    check("active analysis supersedes stale display state",
          analyzingContext.semanticState
                  == EditorActionSemanticState::Analyzing
              && analyzingContext.compactText().contains(
                     QStringLiteral("analyzing")));
}
void runEditorActionContextStripRegression()
{
    MainWindow window;
    window.resize(900, 600);
    window.show();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QLabel* chip = window.findChild<QLabel*>(
        QStringLiteral("editorActionContextChip"));
    check("persistent editor action context chip exists",
          chip != nullptr);
    check("empty editor context remains visibly explicit",
          chip && chip->isVisible()
              && chip->text().contains(QStringLiteral("instance unbound"))
              && chip->toolTip().contains(
                     QStringLiteral("Semantic snapshot")));

    QTemporaryDir temp;
    check("context strip fixture root is available", temp.isValid());
    if (!temp.isValid() || !chip)
        return;
    const QString moduleFile =
        temp.filePath(QStringLiteral("leaf.sv"));
    const QString moduleText =
        QStringLiteral("module leaf; logic payload; endmodule\n");
    QFile moduleSource(moduleFile);
    const bool moduleWritten =
        moduleSource.open(QIODevice::WriteOnly | QIODevice::Text)
        && moduleSource.write(moduleText.toUtf8())
               == moduleText.toUtf8().size();
    moduleSource.close();
    check("context strip module fixture is written", moduleWritten);
    if (moduleWritten)
        window.tabManager->openFileInTab(moduleFile);
    MyCodeEditor* editor = window.tabManager->getCurrentEditor();
    if (editor) {
        QTextCursor cursor(editor->document());
        cursor.setPosition(positionInside(moduleText,
                                          QStringLiteral("payload")));
        editor->setTextCursor(cursor);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    check("context strip follows current Tree-sitter module scope",
          chip->text().contains(QStringLiteral("module leaf"))
              && chip->text().contains(
                     QStringLiteral("instance unbound"))
              && chip->toolTip().contains(
                     QStringLiteral("Syntax revision")));

    const QString packageFile =
        temp.filePath(QStringLiteral("types_pkg.sv"));
    const QString packageText =
        QStringLiteral("package types_pkg; typedef logic word_t; endpackage\n");
    QFile packageSource(packageFile);
    const bool packageWritten =
        packageSource.open(QIODevice::WriteOnly | QIODevice::Text)
        && packageSource.write(packageText.toUtf8())
               == packageText.toUtf8().size();
    packageSource.close();
    check("context strip package fixture is written", packageWritten);
    if (packageWritten)
        window.tabManager->openFileInTab(packageFile);
    editor = window.tabManager->getCurrentEditor();
    if (editor) {
        QTextCursor cursor(editor->document());
        cursor.setPosition(positionInside(packageText,
                                          QStringLiteral("word_t")));
        editor->setTextCursor(cursor);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    check("context strip follows current Tree-sitter package scope",
          chip->text().contains(QStringLiteral("package types_pkg")));
    window.hide();
}

void runEditorActionContextHotPathRegression()
{
    MainWindow window;
    window.resize(900, 600);
    window.show();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QTemporaryDir temp;
    check("action context hot-path fixture root is available",
          temp.isValid());
    if (!temp.isValid())
        return;

    const QString moduleFile =
        temp.filePath(QStringLiteral("hot_path_leaf.sv"));
    const QString moduleText = QStringLiteral(
        "module hot_path_leaf; logic alpha; logic beta; endmodule\n");
    QFile moduleSource(moduleFile);
    const bool moduleWritten =
        moduleSource.open(QIODevice::WriteOnly | QIODevice::Text)
        && moduleSource.write(moduleText.toUtf8())
               == moduleText.toUtf8().size();
    moduleSource.close();
    check("action context hot-path source is written", moduleWritten);
    if (!moduleWritten)
        return;

    QStringList workspaceFiles{moduleFile};
    workspaceFiles.reserve(2049);
    for (int index = 0; index < 2048; ++index) {
        workspaceFiles.append(temp.filePath(
            QStringLiteral("generated_%1.sv").arg(index, 4, 10,
                                                  QLatin1Char('0'))));
    }

    ProjectModel* projectModel =
        window.workspaceManager->getProjectModel();
    ProjectSnapshot project;
    {
        const QSignalBlocker blockProjectSignals(projectModel);
        projectModel->setWorkspaceState(temp.path(), workspaceFiles);
        project = projectModel->snapshot();
    }
    window.workspaceManager->projectChanged(project);
    window.tabManager->openFileInTab(moduleFile);
    MyCodeEditor* editor = window.tabManager->getCurrentEditor();
    check("action context hot-path editor is available", editor != nullptr);
    if (!editor)
        return;

    QTextCursor cursor(editor->document());
    cursor.setPosition(positionInside(moduleText, QStringLiteral("alpha")));
    editor->setTextCursor(cursor);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    window.workspaceManager
        ->resetProjectSnapshotMaterializationCountForTesting();
    EditorActionContextService::resetMetricsForTesting();
    window.resetEditorActionContextChipWriteCountsForTesting();

    const int alphaPosition =
        positionInside(moduleText, QStringLiteral("alpha"));
    const int betaPosition =
        positionInside(moduleText, QStringLiteral("beta"));
    constexpr int refreshIterations = 64;
    for (int iteration = 0; iteration < refreshIterations; ++iteration) {
        QTextCursor moved(editor->document());
        moved.setPosition((iteration % 2) == 0
                              ? betaPosition : alphaPosition);
        editor->setTextCursor(moved);
    }
    const DocumentChange unchangedDocumentRefresh;
    for (int iteration = 0; iteration < refreshIterations; ++iteration)
        editor->documentChangeApplied(unchangedDocumentRefresh);

    const std::uint64_t unchangedSnapshotMaterializations =
        window.workspaceManager
            ->projectSnapshotMaterializationCountForTesting();
    const EditorActionContextServiceMetrics unchangedServiceMetrics =
        EditorActionContextService::metricsForTesting();
    const EditorActionContextChipWriteCounts unchangedChipWrites =
        window.editorActionContextChipWriteCountsForTesting();
    std::printf(
        "action_context_hot_path.unchanged_refreshes=%d "
        "snapshot_materializations=%llu normalizations=%llu sorts=%llu "
        "hierarchy_rebuilds=%llu chip_writes=%llu\n",
        refreshIterations * 2,
        static_cast<unsigned long long>(
            unchangedSnapshotMaterializations),
        static_cast<unsigned long long>(
            unchangedServiceMetrics.workspaceFileNormalizationPasses),
        static_cast<unsigned long long>(
            unchangedServiceMetrics.workspaceFileSortPasses),
        static_cast<unsigned long long>(
            unchangedServiceMetrics.hierarchyCacheRebuilds),
        static_cast<unsigned long long>(unchangedChipWrites.total()));
    check("unchanged cursor/document refresh avoids ProjectSnapshot materialization",
          unchangedSnapshotMaterializations == 0);
    check("unchanged cursor/document refresh avoids workspace normalization and sorting",
          unchangedServiceMetrics.workspaceFileNormalizationPasses == 0
              && unchangedServiceMetrics.workspaceFileSortPasses == 0);
    check("unchanged cursor/document refresh reuses hierarchy candidates",
          unchangedServiceMetrics.hierarchyCacheRebuilds == 0);
    check("unchanged cursor/document refresh performs no chip property writes",
          unchangedChipWrites.total() == 0);

    window.workspaceManager
        ->resetProjectSnapshotMaterializationCountForTesting();
    EditorActionContextService::resetMetricsForTesting();
    window.resetEditorActionContextChipWriteCountsForTesting();

    workspaceFiles.append(
        temp.filePath(QStringLiteral("project_changed_once.sv")));
    {
        const QSignalBlocker blockProjectSignals(projectModel);
        projectModel->setScannedFiles(workspaceFiles);
        project = projectModel->snapshot();
    }
    window.workspaceManager->projectChanged(project);
    window.workspaceManager->projectChanged(project);

    for (int iteration = 0; iteration < refreshIterations; ++iteration)
        editor->documentChangeApplied(unchangedDocumentRefresh);

    const std::uint64_t changedSnapshotMaterializations =
        window.workspaceManager
            ->projectSnapshotMaterializationCountForTesting();
    const EditorActionContextServiceMetrics changedServiceMetrics =
        EditorActionContextService::metricsForTesting();
    const EditorActionContextChipWriteCounts changedChipWrites =
        window.editorActionContextChipWriteCountsForTesting();
    std::printf(
        "action_context_hot_path.project_changed_then_refreshes=%d "
        "snapshot_materializations=%llu normalizations=%llu sorts=%llu "
        "hierarchy_rebuilds=%llu chip_writes=%llu\n",
        refreshIterations,
        static_cast<unsigned long long>(changedSnapshotMaterializations),
        static_cast<unsigned long long>(
            changedServiceMetrics.workspaceFileNormalizationPasses),
        static_cast<unsigned long long>(
            changedServiceMetrics.workspaceFileSortPasses),
        static_cast<unsigned long long>(
            changedServiceMetrics.hierarchyCacheRebuilds),
        static_cast<unsigned long long>(changedChipWrites.total()));
    check("projectChanged rebuild avoids pull-based ProjectSnapshot materialization",
          changedSnapshotMaterializations == 0);
    check("projectChanged normalizes and sorts workspace scope exactly once",
          changedServiceMetrics.workspaceFileNormalizationPasses == 1
              && changedServiceMetrics.workspaceFileSortPasses == 1);
    check("projectChanged invalidates and rebuilds hierarchy candidates exactly once",
          changedServiceMetrics.hierarchyCacheRebuilds == 1);
    check("projectChanged plus unchanged refreshes avoid redundant chip writes",
          changedChipWrites.total() == 0);
    window.hide();
}

void runDialogRegression()
{
    QString untouched = QStringLiteral("module leaf; endmodule");
    ExposeSignalToTopDialog cancelDialog(
        readyDialogReport(QStringLiteral("payload_out")),
        [](const QString& name) {
            ExposeSignalToTopReport report =
                readyDialogReport(name);
            report.renderedDiff =
                QStringLiteral("+ output logic %1").arg(name);
            return report;
        });
    auto* nameEdit = cancelDialog.findChild<QLineEdit*>(
        QStringLiteral("exposeSignalPortName"));
    auto* summary = cancelDialog.findChild<QPlainTextEdit*>(
        QStringLiteral("exposeSignalSummary"));
    auto* diff = cancelDialog.findChild<QPlainTextEdit*>(
        QStringLiteral("exposeSignalDiff"));
    auto* apply = cancelDialog.findChild<QPushButton*>(
        QStringLiteral("exposeSignalApplyButton"));
    auto* cancel = cancelDialog.findChild<QPushButton*>(
        QStringLiteral("exposeSignalCancelButton"));
    check("preview exposes editable final port",
          nameEdit && nameEdit->text()
              == QStringLiteral("payload_out"));
    check("preview lists source path and affected modules",
          summary
              && summary->toPlainText().contains(
                  QStringLiteral("top.u_mid.u_leaf"))
              && summary->toPlainText().contains(
                  QStringLiteral("leaf, mid, top")));
    check("preview renders diff and enables Apply",
          diff && diff->toPlainText().contains(
                      QStringLiteral("leaf.sv"))
              && apply && apply->isEnabled());
    if (nameEdit)
        nameEdit->setText(QStringLiteral("debug_out"));
    check("editing port replans preview",
          cancelDialog.reportForApply().exportedPortName
                  == QStringLiteral("debug_out")
              && diff->toPlainText().contains(
                  QStringLiteral("debug_out")));
    if (cancel)
        QTest::mouseClick(cancel, Qt::LeftButton);
    check("Cancel rejects and performs no mutation",
          cancelDialog.result() == QDialog::Rejected
              && untouched
                  == QStringLiteral("module leaf; endmodule"));
}

void runMenuAvailabilityRegression()
{
    QTemporaryDir temp;
    check("context menu fixture root is available", temp.isValid());
    if (!temp.isValid())
        return;

    const QString fileName =
        temp.filePath(QStringLiteral("expose_menu.sv"));
    const QString text =
        QStringLiteral(
            "module leaf(\n"
            "  input logic clk\n"
            ");\n"
            "  logic [7:0] payload;\n"
            "endmodule\n");
    QFile file(fileName);
    const bool fileWritten =
        file.open(QIODevice::WriteOnly | QIODevice::Text)
        && file.write(text.toUtf8()) == text.toUtf8().size();
    file.close();
    check("context menu fixture source is written", fileWritten);
    if (!fileWritten)
        return;

    SemanticSymbolRecord signal =
        SemanticFixtureRecordBuilder(
            QStringLiteral("payload"),
            SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(fileName)
            .withLocalHandle(301)
            .withLine(4, 15)
            .withTextSpan(text.indexOf(QStringLiteral("payload")), 7)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Logic)
            .inModule(QStringLiteral("leaf"))
            .record();
    SemanticSymbolRecord input =
        SemanticFixtureRecordBuilder(
            QStringLiteral("clk"),
            SymbolTaxonomy::DeclarationKind::Port)
            .withFile(fileName)
            .withLocalHandle(302)
            .withLine(2, 15)
            .withTextSpan(text.indexOf(QStringLiteral("clk")), 3)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::PortInput)
            .inModule(QStringLiteral("leaf"))
            .record();
    SemanticIndex::getInstance()->setSnapshot(
        snapshot({signal, input}, {{fileName, text}}));

    QTabWidget tabs;
    tabs.resize(640, 360);
    TabManager manager(&tabs);
    EditorCoordinator coordinator(&manager);
    check("context menu fixture opens in real TabManager",
          manager.openFileInTab(fileName));
    MyCodeEditor* editor = manager.getCurrentEditor();
    check("context menu fixture creates MyCodeEditor",
          editor != nullptr);
    if (!editor)
        return;
    coordinator.attachEditor(editor);
    tabs.show();
    QCoreApplication::processEvents();

    editor->setHierarchyInstanceContext({
        temp.path(),
        QStringLiteral("top"),
        QStringLiteral("top.u_leaf")});
    const MenuActionState bound =
        contextMenuState(editor,
                         QStringLiteral("payload"),
                         QStringLiteral("payload"));
    check("real context menu exposes Action entry",
          bound.menuShown && bound.found
              && bound.groupTitle == QStringLiteral("Refactor"));
    check("bound module signal enables real context menu Action",
          bound.enabled);

    editor->setHierarchyInstanceContext({
        temp.path(), QString(), QString()});
    const MenuActionState unbound =
        contextMenuState(editor,
                         QStringLiteral("payload"),
                         QStringLiteral("payload"));
    check("unbound instance keeps real context menu Action enterable",
          unbound.found && unbound.enabled
              && !unbound.visibleText.trimmed().isEmpty());

    editor->setHierarchyInstanceContext({
        temp.path(),
        QStringLiteral("top"),
        QStringLiteral("top.u_leaf")});
    const MenuActionState nonPropagatable =
        contextMenuState(editor,
                         QStringLiteral("clk"),
                         QStringLiteral("payload"));
    check("unsupported object keeps Action clickable with visible reason",
          nonPropagatable.found && nonPropagatable.enabled
              && nonPropagatable.visibleText
                     != QStringLiteral("Expose signal to top..."));

    const MenuActionState clickWins =
        contextMenuState(editor,
                         QStringLiteral("payload"),
                         QStringLiteral("clk"));
    check("right-click position, not old cursor, selects target",
          clickWins.found && clickWins.enabled);
}

void runQtApplyChainRegression()
{
    QTemporaryDir temp;
    const QString fileName =
        temp.filePath(QStringLiteral("top.sv"));
    const QString before =
        QStringLiteral("module top;\nendmodule\n");
    QFile file(fileName);
    file.open(QIODevice::WriteOnly | QIODevice::Text);
    file.write(before.toUtf8());
    file.close();

    QTabWidget tabs;
    TabManager manager(&tabs);
    check("fixture opens in real TabManager",
          manager.openFileInTab(fileName));
    MyCodeEditor* editor = manager.getCurrentEditor();
    QSignalSpy edited(manager.getDocumentModel(),
                      &DocumentModel::documentEdited);
    ZeroSlackWorkspaceDocumentManager documents(&manager);
    const auto baseline =
        documents.snapshot(fileName.toUtf8().toStdString());
    check("Qt adapter captures editor baseline",
          baseline.has_value());

    SemanticIndex::getInstance()->setSnapshot(snapshot({}));
    const std::uint64_t generation =
        SemanticIndex::getInstance()->snapshotRevision();
    rtledit::SemanticEditIntent intent;
    intent.kind = rtledit::SemanticEditKind::ExposeSignalToTop;
    intent.target.kind = rtledit::SemanticObjectKind::Signal;
    intent.target.qualifiedName = "top.trace";
    rtledit::WorkspaceTextEdit edit;
    edit.filePath = fileName.toUtf8().toStdString();
    edit.expectedDocumentVersion = baseline->version;
    edit.range = {{1, 0}, {1, 0}};
    edit.newText = "  logic trace;\n";
    rtledit::TextEditProvenance provenance;
    provenance.actionId = rtledit::kExposeSignalToTopActionId;
    provenance.anchorName = "gui.apply";
    provenance.description = "GUI apply chain regression";
    provenance.anchor.source =
        rtledit::AnchorResolutionSource::TreeSitter;
    provenance.editIndex = 0;
    auto plan = rtledit::makeWorkspaceEditPlan(
        intent, rtledit::RiskLevel::High,
        rtledit::PreviewPolicy::Diff,
        {edit}, {provenance});
    plan.semanticSnapshot.id = std::to_string(generation);
    plan.semanticIndexFilePaths = {
        fileName.toUtf8().toStdString()};

    ExposeSignalToTopReport report =
        readyDialogReport(QStringLiteral("trace"));
    report.planResult.plan.workspaceEdit = plan;
    report.sourceDiff = rtledit::buildWorkspaceEditSourceDiff(
        plan, rtledit::SemanticIndexSnapshot{
                  std::to_string(generation)}, documents);
    ExposeSignalToTopDialog applyDialog(report);
    auto* applyButton = applyDialog.findChild<QPushButton*>(
        QStringLiteral("exposeSignalApplyButton"));
    if (applyButton)
        QTest::mouseClick(applyButton, Qt::LeftButton);
    ExposeSignalToTopService service;
    const ExposeSignalToTopApplyReport applied =
        service.apply(applyDialog.reportForApply(), documents);
    check("Apply accepts and executes rtleditcore transaction",
          applyDialog.result() == QDialog::Accepted
              && applied.applied());
    QCoreApplication::processEvents();
    check("Apply follows editor incremental document chain",
          editor
              && editor->cachedDocumentText().contains(
                  QStringLiteral("logic trace"))
              && edited.count() > 0
              && manager.getDocumentForEditor(editor).textVersion
                  > static_cast<int>(baseline->version.value));
}
} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    runEditorActionContextRegression();
    runEditorActionContextStripRegression();
    runEditorActionContextHotPathRegression();
    runDialogRegression();
    runMenuAvailabilityRegression();
    runQtApplyChainRegression();
    std::printf("\n%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
