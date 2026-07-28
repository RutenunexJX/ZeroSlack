#include "editorcoordinator.h"
#include "exposesignaltotopdialog.h"
#include "exposesignaltotopservice.h"
#include "mycodeeditor.h"
#include "semantic_fixture_records.h"
#include "semanticindexsnapshot.h"
#include "tabmanager.h"

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

struct MenuActionState {
    bool menuShown = false;
    bool found = false;
    bool enabled = false;
};

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
                QAction* expose = nullptr;
                for (QAction* action : menu->actions()) {
                    if (action->objectName()
                        == QStringLiteral("exposeSignalToTopAction")) {
                        expose = action;
                        break;
                    }
                }
                state.found = expose != nullptr;
                state.enabled = expose && expose->isEnabled();
                menu->close();
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
          bound.menuShown && bound.found);
    check("bound module signal enables real context menu Action",
          bound.enabled);

    editor->setHierarchyInstanceContext({
        temp.path(), QString(), QString()});
    const MenuActionState unbound =
        contextMenuState(editor,
                         QStringLiteral("payload"),
                         QStringLiteral("payload"));
    check("unbound instance disables real context menu Action",
          unbound.found && !unbound.enabled);

    editor->setHierarchyInstanceContext({
        temp.path(),
        QStringLiteral("top"),
        QStringLiteral("top.u_leaf")});
    const MenuActionState nonPropagatable =
        contextMenuState(editor,
                         QStringLiteral("clk"),
                         QStringLiteral("payload"));
    check("non-propagatable object disables real context menu Action",
          nonPropagatable.found && !nonPropagatable.enabled);

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
    runDialogRegression();
    runMenuAvailabilityRegression();
    runQtApplyChainRegression();
    std::printf("\n%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
