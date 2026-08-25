#include "actionregistry.h"
#include "applicationthememanager.h"
#include "fsmgraphservice.h"
#include "graphexportui.h"
#include "mycodeeditor.h"
#include "rtlinsightspanelcoordinator.h"
#include "semantic_fixture_records.h"
#include "semanticindex.h"
#include "semanticindexsnapshot.h"
#include "semanticpanelrefreshcoordinator.h"

#include <QApplication>
#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QLineF>
#include <QScrollBar>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolButton>
#include <QTransform>
#include <QWidget>

#include <algorithm>
#include <iostream>
#include <memory>

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

QString fixtureText()
{
    QString text = QStringLiteral(
        "module fsm_top(input logic clk, input logic rst_n, input logic start);\n"
        "  typedef enum logic [1:0] {IDLE, RUN, DONE} state_t;\n"
        "  state_t state_q;\n"
        "  state_t state_d;\n"
        "  always_ff @(posedge clk or negedge rst_n) begin\n"
        "    if (!rst_n) state_q <= IDLE;\n"
        "    else state_q <= state_d;\n"
        "  end\n"
        "  always_comb begin\n"
        "    state_d = state_q;\n"
        "    case (state_q)\n"
        "      IDLE: if (start) state_d = RUN;\n"
        "      RUN: state_d = DONE;\n"
        "      DONE: if (!start) state_d = IDLE;\n"
        "      default: state_d = IDLE;\n"
        "    endcase\n"
        "  end\n"
        "endmodule\n");
    for (int line = 0; line < 180; ++line) {
        text.append(
            QStringLiteral("// scroll-only filler %1\n")
                .arg(line));
    }
    return text;
}

QList<SemanticSymbolRecord> fixtureRecords(
    const QString& fileName)
{
    using CollectorKind =
        SymbolTaxonomy::CollectorKind;
    using DeclarationKind =
        SymbolTaxonomy::DeclarationKind;

    const SemanticSymbolRecord module =
        SemanticFixtureRecordBuilder(
            QStringLiteral("fsm_top"),
            DeclarationKind::Module)
            .withFile(fileName)
            .withLocalHandle(1001)
            .withRange(1, 1, 18, 1)
            .withCollectorKind(
                CollectorKind::Module)
            .record();
    const SemanticSymbolRecord stateQ =
        SemanticFixtureRecordBuilder(
            QStringLiteral("state_q"),
            DeclarationKind::Enum)
            .withFile(fileName)
            .withLocalHandle(1002)
            .withLine(3)
            .withCollectorKind(
                CollectorKind::EnumVariable)
            .inModule(QStringLiteral("fsm_top"))
            .withType(QStringLiteral("state_t"))
            .record();
    const SemanticSymbolRecord stateD =
        SemanticFixtureRecordBuilder(
            QStringLiteral("state_d"),
            DeclarationKind::Enum)
            .withFile(fileName)
            .withLocalHandle(1003)
            .withLine(4)
            .withCollectorKind(
                CollectorKind::EnumVariable)
            .inModule(QStringLiteral("fsm_top"))
            .withType(QStringLiteral("state_t"))
            .record();
    const auto enumValue =
        [&fileName](const QString& name,
                    int handle) {
            return SemanticFixtureRecordBuilder(
                       name,
                       DeclarationKind::Enum)
                .withFile(fileName)
                .withLocalHandle(handle)
                .withLine(2)
                .withCollectorKind(
                    CollectorKind::EnumValue)
                .inModule(
                    QStringLiteral("fsm_top"))
                .withType(
                    QStringLiteral("state_t"))
                .record();
        };
    return {
        module,
        stateQ,
        stateD,
        enumValue(QStringLiteral("IDLE"), 1004),
        enumValue(QStringLiteral("RUN"), 1005),
        enumValue(QStringLiteral("DONE"), 1006)};
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    const QString fileName =
        QDir::cleanPath(
            QDir::current().absoluteFilePath(
                QStringLiteral(
                    "test_sv/rtl_insight_linkage_fixture.sv")));
    const QString source = fixtureText();
    QHash<QString, QString> fileContents;
    fileContents.insert(fileName, source);
    SemanticIndex index;
    index.setSnapshot(
        std::make_shared<SemanticIndexSnapshot>(
            SemanticIndexSnapshot::fromSymbolRecords(
                fixtureRecords(fileName),
                {},
                {},
                fileContents)));
    FsmGraphService::getInstance()
        ->setSemanticIndex(&index);

    QWidget host;
    RtlInsightsPanelCoordinator panel(&host);
    RtlInsightSourceLocation initial;
    initial.fileName = fileName;
    initial.line = 4;
    initial.column = 3;
    initial.moduleName = QStringLiteral("fsm_top");
    initial.symbolName = QStringLiteral("state_d");
    initial.workspacePath =
        QFileInfo(fileName).absolutePath();
    initial.activeTopModule =
        QStringLiteral("soc_top");
    initial.instancePath =
        QStringLiteral("soc_top.u_fsm");
    initial.documentRevision = 7;
    panel.syncSourceLocation(initial);
    panel.showFsmGraph();
    QApplication::processEvents();

    check(panel.graphNodeItemCountForTest() >= 3,
          "state graph fixture materializes nodes");
    check(panel.graphBuildGenerationForTest() == 1,
          "first explicit graph build advances generation once");
    check(panel.graphModeForTest()
              == QStringLiteral("fsm"),
          "state graph mode is retained");
    const QStringList initialGraphSummaries =
        panel.graphElementSummariesForTest();
    const bool initialStateMarked = std::any_of(
              initialGraphSummaries.cbegin(),
              initialGraphSummaries.cend(),
              [](const QString& summary) {
                  return summary.startsWith(
                      QStringLiteral("state|IDLE|initial"));
              });
    if (!initialStateMarked) {
        std::cerr << "RTL initial state diagnostic: ["
                  << initialGraphSummaries.join(
                         QStringLiteral(" || ")).toStdString()
                  << "]\n";
    }
    check(initialStateMarked,
          "state graph marks the deterministic layout root as initial");
    QToolButton* pinButton =
        panel.pinButtonForTest();
    check(pinButton && pinButton->isCheckable(),
          "RTL Insights exposes a checkable Pin control");

    ApplicationThemeManager::instance().setMode(ThemeMode::Light);
    panel.refreshThemePresentation();
    // Finish the initial Light layout before recording viewport state. This
    // prevents the Dark-side event pump from also settling the baseline UI.
    QApplication::processEvents();
    panel.setFocusSearchText(QStringLiteral("RUN"));
    check(panel.selectGraphItemForTest(
              QStringLiteral("state"),
              QStringLiteral("RUN")),
          "theme fixture selects a graph node");
    check(panel.setGraphItemHoveredForTest(
              QStringLiteral("state"),
              QStringLiteral("IDLE"),
              QString(),
              true),
          "theme fixture records a hovered graph node");
    QGraphicsView* themeGraphView = panel.graphView();
    if (themeGraphView) {
        themeGraphView->setTransform(
            QTransform::fromScale(1.37, 1.37));
        themeGraphView->centerOn(
            themeGraphView->scene()->sceneRect().center()
                + QPointF(23.0, 11.0));
    }
    const QTransform lightTransform = themeGraphView
        ? themeGraphView->transform() : QTransform();
    const QPointF lightCenter = themeGraphView
        ? themeGraphView->mapToScene(
              themeGraphView->viewport()->rect().center())
        : QPointF();
    const QSize lightViewportSize = themeGraphView
        ? themeGraphView->viewport()->size() : QSize();
    const QStringList lightVisuals =
        panel.graphElementVisualSummariesForTest();
    const QStringList lightSelection =
        panel.graphSelectedElementSummariesForTest();
    const QStringList lightHovered =
        panel.graphHoveredElementSummariesForTest();
    const quint64 themeGeneration =
        panel.graphBuildGenerationForTest();
    const quint64 themeBuildRequests =
        panel.graphBuildRequestCountForTest();

    ApplicationThemeManager::instance().setMode(ThemeMode::Dark);
    panel.refreshThemePresentation();
    QApplication::processEvents();
    const QPointF darkCenter = themeGraphView
        ? themeGraphView->mapToScene(
              themeGraphView->viewport()->rect().center())
        : QPointF();
    const QSize darkViewportSize = themeGraphView
        ? themeGraphView->viewport()->size() : QSize();
    check(panel.graphBuildRequestCountForTest()
                  == themeBuildRequests
              && panel.graphBuildGenerationForTest()
                     == themeGeneration,
          "theme refresh consumes the cached RTL graph without service or semantic rebuild");
    const QStringList darkSelection =
        panel.graphSelectedElementSummariesForTest();
    const QStringList darkHovered =
        panel.graphHoveredElementSummariesForTest();
    const bool darkInteractionStatePreserved =
        panel.focusSearchText() == QStringLiteral("RUN")
        && darkSelection == lightSelection
        && darkHovered == lightHovered;
    if (!darkInteractionStatePreserved) {
        std::cerr
            << "RTL theme state diagnostic: search='"
            << panel.focusSearchText().toStdString()
            << "', light selection=["
            << lightSelection.join(QStringLiteral(" || ")).toStdString()
            << "], dark selection=["
            << darkSelection.join(QStringLiteral(" || ")).toStdString()
            << "], light hover=["
            << lightHovered.join(QStringLiteral(" || ")).toStdString()
            << "], dark hover=["
            << darkHovered.join(QStringLiteral(" || ")).toStdString()
            << "]\n";
    }
    check(darkInteractionStatePreserved,
          "dark theme preserves RTL graph search, selection, and hover state");
    const bool darkViewportStatePreserved =
        themeGraphView
        && themeGraphView->transform() == lightTransform
        && QLineF(lightCenter, darkCenter).length() < 1.0;
    if (!darkViewportStatePreserved) {
        const QTransform darkTransform = themeGraphView
            ? themeGraphView->transform() : QTransform();
        std::cerr
            << "RTL theme viewport diagnostic: light scale=("
            << lightTransform.m11() << ',' << lightTransform.m22()
            << "), dark scale=(" << darkTransform.m11() << ','
            << darkTransform.m22() << "), light size="
            << lightViewportSize.width() << 'x'
            << lightViewportSize.height() << ", dark size="
            << darkViewportSize.width() << 'x'
            << darkViewportSize.height() << ", light center=("
            << lightCenter.x() << ',' << lightCenter.y()
            << "), dark center=(" << darkCenter.x() << ','
            << darkCenter.y() << "), distance="
            << QLineF(lightCenter, darkCenter).length() << '\n';
    }
    check(darkViewportStatePreserved,
          "dark theme preserves RTL graph transform and center");
    check(panel.graphElementVisualSummariesForTest()
                  != lightVisuals
              && panel.graphItemsReadableForTest(),
          "dark theme recolors cached RTL graph items readably");

    ApplicationThemeManager::instance().setMode(ThemeMode::Light);
    panel.refreshThemePresentation();
    QApplication::processEvents();
    const QStringList restoredLightVisuals =
        panel.graphElementVisualSummariesForTest();
    const bool lightVisualsRestored =
        restoredLightVisuals == lightVisuals
              && panel.graphBuildRequestCountForTest()
                     == themeBuildRequests
              && panel.graphBuildGenerationForTest()
                     == themeGeneration;
    if (!lightVisualsRestored) {
        std::cerr
            << "RTL theme round-trip diagnostic: original=["
            << lightVisuals.join(QStringLiteral(" || ")).toStdString()
            << "], restored=["
            << restoredLightVisuals.join(QStringLiteral(" || ")).toStdString()
            << "], build requests="
            << panel.graphBuildRequestCountForTest() << '/'
            << themeBuildRequests << ", generation="
            << panel.graphBuildGenerationForTest() << '/'
            << themeGeneration << '\n';
    }
    check(lightVisualsRestored,
          "Light-Dark-Light restores RTL graph visuals without rebuilding its model");

    if (pinButton)
        pinButton->click();
    check(panel.isPinned()
              && pinButton
              && pinButton->isChecked()
              && pinButton->text()
                     == QStringLiteral("Pinned"),
          "visible Pin control updates the canonical pin state");
    const quint64 pinnedGeneration =
        panel.graphBuildGenerationForTest();
    const QStringList pinnedElements =
        panel.graphElementSummariesForTest();

    RtlInsightSourceLocation other = initial;
    other.fileName =
        QDir::cleanPath(
            QDir::current().absoluteFilePath(
                QStringLiteral("other.sv")));
    other.moduleName = QStringLiteral("other");
    other.symbolName = QStringLiteral("unrelated");
    check(!panel.syncSourceLocation(other),
          "unrelated pinned source selection does not select a graph item");
    check(panel.graphBuildGenerationForTest()
              == pinnedGeneration
              && panel.graphElementSummariesForTest()
                     == pinnedElements,
          "pin prevents passive context changes from replacing the graph");
    check(panel.currentFileNameForTest() == fileName
              && panel.currentModuleNameForTest()
                     == QStringLiteral("fsm_top")
              && panel.currentSignalNameForTest()
                     == QStringLiteral("state_d"),
          "pin preserves the displayed file, module, and signal context");

    check(panel.selectGraphItemForTest(
              QStringLiteral("state"),
              QStringLiteral("RUN")),
          "refresh fixture selects a stable graph identity");
    QGraphicsView* refreshGraphView = panel.graphView();
    if (refreshGraphView) {
        refreshGraphView->setTransform(
            QTransform::fromScale(1.41, 1.41));
        refreshGraphView->centerOn(
            refreshGraphView->scene()->sceneRect().center()
                + QPointF(19.0, -13.0));
    }
    const QTransform beforeRefreshTransform = refreshGraphView
        ? refreshGraphView->transform() : QTransform();
    const QPointF beforeRefreshCenter = refreshGraphView
        ? refreshGraphView->mapToScene(
              refreshGraphView->viewport()->rect().center())
        : QPointF();
    const QStringList beforeRefreshSelection =
        panel.graphSelectedElementSummariesForTest();
    panel.refresh();
    check(panel.graphBuildGenerationForTest()
              == pinnedGeneration + 1,
          "an explicit graph refresh remains available while pinned");
    check(panel.isPinned(),
          "explicit refresh does not silently clear pin state");
    const QPointF afterRefreshCenter = refreshGraphView
        ? refreshGraphView->mapToScene(
              refreshGraphView->viewport()->rect().center())
        : QPointF();
    const QTransform afterRefreshTransform = refreshGraphView
        ? refreshGraphView->transform() : QTransform();
    const QStringList afterRefreshSelection =
        panel.graphSelectedElementSummariesForTest();
    const bool compatibleRefreshPreserved = refreshGraphView
        && afterRefreshTransform == beforeRefreshTransform
        && QLineF(beforeRefreshCenter, afterRefreshCenter).length() < 1.0
        && afterRefreshSelection == beforeRefreshSelection;
    if (!compatibleRefreshPreserved) {
        std::cerr << "RTL compatible refresh diagnostic: before scale=("
                  << beforeRefreshTransform.m11() << ','
                  << beforeRefreshTransform.m22() << "), after scale=("
                  << afterRefreshTransform.m11() << ','
                  << afterRefreshTransform.m22() << "), before center=("
                  << beforeRefreshCenter.x() << ','
                  << beforeRefreshCenter.y() << "), after center=("
                  << afterRefreshCenter.x() << ','
                  << afterRefreshCenter.y() << "), distance="
                  << QLineF(beforeRefreshCenter, afterRefreshCenter).length()
                  << ", before selection=["
                  << beforeRefreshSelection.join(
                         QStringLiteral(" || ")).toStdString()
                  << "], after selection=["
                  << afterRefreshSelection.join(
                         QStringLiteral(" || ")).toStdString()
                  << "]\n";
    }
    check(compatibleRefreshPreserved,
          "compatible graph refresh preserves viewport and stable selection");

    RtlInsightSourceLocation activated;
    panel.setSourceNavigationHandler(
        [&activated](
            const RtlInsightSourceLocation& location) {
            activated = location;
            return true;
        });
    check(panel.triggerGraphNavigationForTest(
              QStringLiteral("state"),
              QStringLiteral("IDLE")),
          "graph node activation routes through rich source navigation");
    check(activated.fileName == fileName
              && activated.line == 2
              && activated.column == 1,
          "node activation carries its exact source position");
    check(activated.moduleName
                  == QStringLiteral("fsm_top")
              && activated.workspacePath
                     == initial.workspacePath
              && activated.activeTopModule
                     == initial.activeTopModule
              && activated.instancePath
                     == initial.instancePath,
          "node activation preserves module and hierarchy instance context");
    check(activated.elementKind
                  == QStringLiteral("state")
              && activated.symbolName
                     == QStringLiteral("IDLE")
              && activated.documentRevision
                     == initial.documentRevision
              && activated.graphGeneration
                     == panel.graphBuildGenerationForTest(),
          "node activation identifies the exact graph element, revision, and generation");

    const quint64 linkedGeneration =
        panel.graphBuildGenerationForTest();
    check(panel.syncSourceLocation(activated),
          "source selection locates the existing graph node");
    check(panel.graphSelectedItemCountForTest() > 0
              && panel.graphBuildGenerationForTest()
                     == linkedGeneration,
          "source-to-graph selection changes selection without rebuilding");
    QAction* jumpAction = panel.graphActionForTest(
        QString::fromLatin1(
            ActionIds::GraphJumpSelected));
    QAction* focusAction = panel.graphActionForTest(
        QString::fromLatin1(
            ActionIds::GraphFocusSelected));
    QAction* setTopAction = panel.graphActionForTest(
        QString::fromLatin1(
            ActionIds::GraphSetTopSelected));
    QAction* temporaryEditorAction =
        panel.graphActionForTest(
            QString::fromLatin1(
                ActionIds::ViewTemporaryEditorOpen));
    check(jumpAction
              && focusAction
              && setTopAction
              && temporaryEditorAction
              && jumpAction->text()
                     == QStringLiteral("Jump")
              && focusAction->text()
                     == QStringLiteral("Focus")
              && setTopAction->text()
                     == QStringLiteral("Set Top")
              && jumpAction->property(
                     GraphExportUi::kActionIdProperty)
                     .toString()
                     == QString::fromLatin1(
                         ActionIds::GraphJumpSelected)
              && jumpAction->property(
                     GraphExportUi::kExecutionRouteProperty)
                     .toString()
                     == QStringLiteral(
                         "insight.graph.jumpSelected"),
          "graph selection controls materialize Registry labels and routes");
    QString temporaryActionId;
    QVariantMap temporaryActionParameters;
    panel.setRegisteredActionRequestHandler(
        [&](const QString& actionId,
            const QVariantMap& parameters) {
            temporaryActionId = actionId;
            temporaryActionParameters = parameters;
            ActionExecutionResult result;
            result.handled = true;
            result.succeeded = true;
            return result;
        });
    const ActionExecutionResult temporaryOpened =
        panel.triggerGraphActionForTest(
            QString::fromLatin1(
                ActionIds::ViewTemporaryEditorOpen));
    check(temporaryOpened.handled
              && temporaryOpened.succeeded
              && temporaryActionId
                     == QString::fromLatin1(
                         ActionIds::ViewTemporaryEditorOpen)
              && temporaryActionParameters
                     .value(QStringLiteral("path"))
                     .toString()
                     == fileName
              && temporaryActionParameters
                     .value(QStringLiteral("line"))
                     .toInt()
                     == 2
              && temporaryActionParameters
                     .value(QStringLiteral("symbolId"))
                     .toString()
                     == QStringLiteral("IDLE")
              && !temporaryActionParameters
                      .value(QStringLiteral("sourceLinkId"))
                      .toString()
                      .isEmpty(),
          "RTL graph More menu dispatches selected source through the unified temporary-editor Action");
    check(jumpAction && jumpAction->isEnabled()
              && focusAction && focusAction->isEnabled()
              && setTopAction && !setTopAction->isEnabled(),
          "FSM selection enables Jump and Focus but rejects Set Top");
    const ActionExecutionResult focused =
        panel.triggerGraphActionForTest(
            QString::fromLatin1(
                ActionIds::GraphFocusSelected));
    check(focused.handled && focused.succeeded,
          "registered Focus Action centers the selected graph item");
    activated = RtlInsightSourceLocation();
    if (jumpAction)
        jumpAction->trigger();
    QApplication::processEvents();
    check(activated.fileName == fileName
              && activated.symbolName
                     == QStringLiteral("IDLE")
              && activated.graphGeneration
                     == linkedGeneration,
          "registered Jump QAction preserves source and graph identity");
    const ActionExecutionResult rejectedSetTop =
        panel.triggerGraphActionForTest(
            QString::fromLatin1(
                ActionIds::GraphSetTopSelected));
    check(rejectedSetTop.handled
              && !rejectedSetTop.succeeded
              && !rejectedSetTop.failureReason.isEmpty(),
          "registered Set Top Action reports unavailable outside module-block mode");
    RtlInsightSourceLocation stale = activated;
    stale.graphGeneration =
        linkedGeneration - 1;
    check(!panel.syncSourceLocation(stale),
          "stale graph-generation source selection is rejected");
    check(panel.graphBuildGenerationForTest()
              == linkedGeneration,
          "stale selection rejection does not rebuild the graph");

    panel.setPinned(false);
    check(!panel.isPinned()
              && pinButton
              && !pinButton->isChecked()
              && pinButton->text()
                     == QStringLiteral("Pin"),
          "unpin updates the visible control");

    MyCodeEditor editor;
    editor.resize(520, 180);
    editor.setDocumentFileName(fileName);
    editor.setPlainText(source);
    HierarchyInstanceContext editorContext;
    editorContext.workspacePath =
        initial.workspacePath;
    editorContext.activeTopModule =
        initial.activeTopModule;
    editorContext.instancePath =
        initial.instancePath;
    editor.setHierarchyInstanceContext(editorContext);
    editor.show();
    QApplication::processEvents();

    SemanticPanelRefreshCoordinator refresh(
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        &panel,
        nullptr);
    refresh.configurePanels();
    refresh.handleActiveEditorChanged(&editor);
    QApplication::processEvents();
    const quint64 beforeEditorScroll =
        panel.graphBuildGenerationForTest();
    check(panel.graphModeForTest()
              == QStringLiteral("fsm"),
          "editor source selection keeps the materialized graph surface");
    check(panel.selectGraphItemForTest(
              QStringLiteral("state"),
              QStringLiteral("RUN")),
          "precondition selects a different graph node");
    QTextCursor cursor =
        editor.document()->find(
            QStringLiteral("IDLE"));
    editor.setTextCursor(cursor);
    QApplication::processEvents();
    const QStringList selectedFromEditor =
        panel.graphSelectedElementSummariesForTest();
    bool editorSelectedIdle = false;
    for (const QString& summary :
         selectedFromEditor) {
        editorSelectedIdle =
            editorSelectedIdle
            || summary.startsWith(
                QStringLiteral("state|IDLE|"));
    }
    check(editorSelectedIdle,
          "editor cursor selection locates the existing IDLE graph node");
    check(panel.graphBuildGenerationForTest()
              == beforeEditorScroll,
          "editor-to-graph selection does not rebuild the graph");

    QScrollBar* scrollBar =
        editor.verticalScrollBar();
    check(scrollBar && scrollBar->maximum() > 0,
          "editor fixture has a measurable scroll range");
    if (scrollBar)
        scrollBar->setValue(scrollBar->maximum());
    QApplication::processEvents();
    check(scrollBar
              && scrollBar->value()
                     == scrollBar->maximum(),
          "editor scroll changes only the viewport position");
    check(panel.graphBuildGenerationForTest()
                  == beforeEditorScroll
              && panel.graphModeForTest()
                     == QStringLiteral("fsm"),
          "pure editor scrolling leaves graph build generation unchanged");

    FsmGraphService::getInstance()
        ->setSemanticIndex(
            SemanticIndex::getInstance());
    std::cout << (checks - failures) << "/"
              << checks
              << " RTL Insight linkage checks passed\n";
    return failures == 0 ? 0 : 1;
}
