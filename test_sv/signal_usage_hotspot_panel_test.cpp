#include "actionregistry.h"
#include "graphexportui.h"
#include "signalusagehotspotpanel.h"
#include "semantic_fixture_records.h"
#include "semanticindexsnapshot.h"

#include <QApplication>
#include <QAction>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFont>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QPushButton>
#include <QPixmap>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <functional>
#include <memory>

namespace {
bool waitUntil(const std::function<bool()>& predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (predicate())
            return true;
        QThread::msleep(1);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return predicate();
}

SignalUsageHotspotItem item(SignalUsageHotspotRole role,
                            const QString& moduleName,
                            const QString& fileName,
                            int line,
                            const QString& snippet)
{
    SignalUsageHotspotItem result;
    result.role = role;
    result.moduleName = moduleName;
    result.fileName = fileName;
    result.line = line;
    result.column = 3;
    result.snippet = snippet;
    result.evidenceText = QStringLiteral("evidence");
    result.roleReasonDisplayName =
        SignalUsageHotspotService::roleDisplayName(role);
    return result;
}

SignalUsageHotspotReport sampleReport()
{
    SignalUsageHotspotReport report;
    report.found = true;
    report.declarationDisplayName = QStringLiteral("mcs");
    report.items.append(item(SignalUsageHotspotRole::Write,
                             QStringLiteral("chl_ctrl"),
                             QStringLiteral("chl_ctrl.sv"),
                             18,
                             QStringLiteral("mcs <= next_mcs;")));
    report.items.append(item(SignalUsageHotspotRole::Read,
                             QStringLiteral("chl_ctrl"),
                             QStringLiteral("chl_ctrl.sv"),
                             42,
                             QStringLiteral("if (mcs) begin")));

    SignalUsageHotspotTrackLane lane;
    lane.moduleName = QStringLiteral("chl_ctrl");
    lane.fileName = QStringLiteral("chl_ctrl.sv");
    lane.startLine = 18;
    lane.endLine = 42;
    lane.count = 2;
    for (int i = 0; i < report.items.size(); ++i) {
        SignalUsageHotspotTrackPosition position;
        position.itemIndex = i;
        position.role = report.items.at(i).role;
        position.roleDisplayName =
            SignalUsageHotspotService::roleDisplayName(position.role);
        position.line = report.items.at(i).line;
        position.column = report.items.at(i).column;
        lane.positions.append(position);
    }
    report.trackLanes.append(lane);

    for (SignalUsageHotspotRole role :
         {SignalUsageHotspotRole::Write, SignalUsageHotspotRole::Read}) {
        SignalUsageHotspotMatrixCell cell;
        cell.moduleName = QStringLiteral("chl_ctrl");
        cell.fileName = QStringLiteral("chl_ctrl.sv");
        cell.role = role;
        cell.roleDisplayName = SignalUsageHotspotService::roleDisplayName(role);
        cell.count = 1;
        report.matrixCells.append(cell);
    }

    return report;
}

SignalUsageHotspotReport visualPreviewReport()
{
    SignalUsageHotspotReport report;
    report.found = true;
    report.declarationDisplayName = QStringLiteral("reg_active_inj_layer");
    auto addRoleSummary = [&report](SignalUsageHotspotRole role, int count) {
        SignalUsageHotspotRoleSummary summary;
        summary.role = role;
        summary.roleDisplayName = SignalUsageHotspotService::roleDisplayName(role);
        summary.count = count;
        report.roleSummaries.append(summary);
    };
    addRoleSummary(SignalUsageHotspotRole::Write, 3);
    addRoleSummary(SignalUsageHotspotRole::Read, 5);
    addRoleSummary(SignalUsageHotspotRole::Port, 1);
    addRoleSummary(SignalUsageHotspotRole::Condition, 2);

    struct UsageSpec {
        SignalUsageHotspotRole role;
        QString moduleName;
        QString fileName;
        int line;
        QString snippet;
    };
    const QList<UsageSpec> usages{
        {SignalUsageHotspotRole::Write, QStringLiteral("chl_ctrl"), QStringLiteral("chl_ctrl.sv"), 178, QStringLiteral("reg_active_inj_layer <= next_layer;")},
        {SignalUsageHotspotRole::Write, QStringLiteral("chl_ctrl"), QStringLiteral("chl_ctrl.sv"), 185, QStringLiteral("active_inj_layer = reg_active_inj_layer;")},
        {SignalUsageHotspotRole::Read, QStringLiteral("chl_ctrl"), QStringLiteral("chl_ctrl.sv"), 112, QStringLiteral("if (reg_active_inj_layer)")},
        {SignalUsageHotspotRole::Read, QStringLiteral("chl_ctrl"), QStringLiteral("chl_ctrl.sv"), 124, QStringLiteral(".active_inj_layer(reg_active_inj_layer)")},
        {SignalUsageHotspotRole::Read, QStringLiteral("chl_ctrl"), QStringLiteral("chl_ctrl.sv"), 156, QStringLiteral("case (reg_active_inj_layer)")},
        {SignalUsageHotspotRole::Read, QStringLiteral("chl_ctrl"), QStringLiteral("chl_ctrl.sv"), 190, QStringLiteral("if (mcs == M_IDLE)")},
        {SignalUsageHotspotRole::Read, QStringLiteral("chl_ctrl"), QStringLiteral("chl_ctrl.sv"), 212, QStringLiteral("if (reg_active_inj_layer && enable)")},
        {SignalUsageHotspotRole::Read, QStringLiteral("chl_ctrl"), QStringLiteral("chl_ctrl.sv"), 402, QStringLiteral("if (reg_active_inj_layer)")},
        {SignalUsageHotspotRole::Read, QStringLiteral("chl_ctrl"), QStringLiteral("chl_ctrl.sv"), 518, QStringLiteral("active_inj_layer(reg_active_inj_layer)")},
        {SignalUsageHotspotRole::Read, QStringLiteral("chl_ctrl"), QStringLiteral("chl_ctrl.sv"), 633, QStringLiteral("if (reg_active_inj_layer)")},
        {SignalUsageHotspotRole::Read, QStringLiteral("chl_ctrl"), QStringLiteral("chl_ctrl.sv"), 701, QStringLiteral("unique case (reg_active_inj_layer)")},
        {SignalUsageHotspotRole::Read, QStringLiteral("chl_ctrl"), QStringLiteral("chl_ctrl.sv"), 812, QStringLiteral("if (reg_active_inj_layer)")},
        {SignalUsageHotspotRole::Read, QStringLiteral("chl_ctrl"), QStringLiteral("chl_ctrl.sv"), 890, QStringLiteral("active_inj_layer(reg_active_inj_layer)")},
        {SignalUsageHotspotRole::Read, QStringLiteral("chl_ctrl"), QStringLiteral("chl_ctrl.sv"), 905, QStringLiteral("if (reg_active_inj_layer)")},
        {SignalUsageHotspotRole::Condition, QStringLiteral("chl_ctrl"), QStringLiteral("chl_ctrl.sv"), 212, QStringLiteral("if (reg_active_inj_layer && enable)")},
        {SignalUsageHotspotRole::Port, QStringLiteral("top_ctrl"), QStringLiteral("top_ctrl.sv"), 124, QStringLiteral(".active_inj_layer(reg_active_inj_layer)")},
        {SignalUsageHotspotRole::Read, QStringLiteral("top_ctrl"), QStringLiteral("top_ctrl.sv"), 402, QStringLiteral("case (reg_active_inj_layer)")},
        {SignalUsageHotspotRole::Case, QStringLiteral("rtl_top"), QStringLiteral("rtl_top.sv"), 633, QStringLiteral("unique case (reg_active_inj_layer)")},
        {SignalUsageHotspotRole::Timing, QStringLiteral("rtl_top"), QStringLiteral("rtl_top.sv"), 812, QStringLiteral("always_ff @(posedge clk)")},
        {SignalUsageHotspotRole::Write, QStringLiteral("prot_inj"), QStringLiteral("prot_inj.sv"), 90, QStringLiteral("reg_active_inj_layer <= injected_layer;")},
        {SignalUsageHotspotRole::Read, QStringLiteral("prot_inj"), QStringLiteral("prot_inj.sv"), 220, QStringLiteral("assign active = reg_active_inj_layer;")},
    };
    for (const UsageSpec& usage : usages)
        report.items.append(item(usage.role, usage.moduleName, usage.fileName, usage.line, usage.snippet));

    struct LaneSpec {
        QString moduleName;
        QString fileName;
        int endLine;
    };
    const QList<LaneSpec> lanes{
        {QStringLiteral("chl_ctrl"), QStringLiteral("chl_ctrl.sv"), 924},
        {QStringLiteral("top_ctrl"), QStringLiteral("top_ctrl.sv"), 612},
        {QStringLiteral("rtl_top"), QStringLiteral("rtl_top.sv"), 1043},
        {QStringLiteral("prot_inj"), QStringLiteral("prot_inj.sv"), 387},
        {QStringLiteral("pkg_global"), QStringLiteral("PKG_global.sv"), 96},
    };
    for (const LaneSpec& spec : lanes) {
        SignalUsageHotspotTrackLane lane;
        lane.moduleName = spec.moduleName;
        lane.fileName = spec.fileName;
        lane.startLine = 1;
        lane.endLine = spec.endLine;
        for (int i = 0; i < report.items.size(); ++i) {
            const SignalUsageHotspotItem& usage = report.items.at(i);
            if (usage.moduleName != spec.moduleName)
                continue;
            SignalUsageHotspotTrackPosition position;
            position.itemIndex = i;
            position.role = usage.role;
            position.roleDisplayName =
                SignalUsageHotspotService::roleDisplayName(usage.role);
            position.line = usage.line;
            position.column = usage.column;
            lane.positions.append(position);
            ++lane.count;
        }
        report.trackLanes.append(lane);
    }
    auto addMatrixCell = [&report](const QString& moduleName,
                                   const QString& fileName,
                                   SignalUsageHotspotRole role,
                                   int count) {
        SignalUsageHotspotMatrixCell cell;
        cell.moduleName = moduleName;
        cell.fileName = fileName;
        cell.role = role;
        cell.roleDisplayName = SignalUsageHotspotService::roleDisplayName(role);
        cell.count = count;
        report.matrixCells.append(cell);
    };
    struct MatrixRowSpec {
        QString moduleName;
        QString fileName;
        int write;
        int read;
        int port;
        int condition;
        int caseCount;
        int timing;
        int unknown;
    };
    const QList<MatrixRowSpec> matrixRows{
        {QStringLiteral("chl_ctrl"), QStringLiteral("chl_ctrl.sv"), 3, 12, 1, 2, 1, 4, 6},
        {QStringLiteral("top_ctrl"), QStringLiteral("top_ctrl.sv"), 0, 4, 1, 0, 1, 2, 2},
        {QStringLiteral("rtl_top"), QStringLiteral("rtl_top.sv"), 0, 7, 0, 1, 2, 5, 3},
        {QStringLiteral("prot_inj"), QStringLiteral("prot_inj.sv"), 0, 2, 0, 0, 0, 1, 1},
        {QStringLiteral("pkg_global"), QStringLiteral("PKG_global.sv"), 0, 0, 0, 0, 0, 0, 0},
    };
    for (const MatrixRowSpec& row : matrixRows) {
        addMatrixCell(row.moduleName, row.fileName, SignalUsageHotspotRole::Write, row.write);
        addMatrixCell(row.moduleName, row.fileName, SignalUsageHotspotRole::Read, row.read);
        addMatrixCell(row.moduleName, row.fileName, SignalUsageHotspotRole::Port, row.port);
        addMatrixCell(row.moduleName, row.fileName, SignalUsageHotspotRole::Condition, row.condition);
        addMatrixCell(row.moduleName, row.fileName, SignalUsageHotspotRole::Case, row.caseCount);
        addMatrixCell(row.moduleName, row.fileName, SignalUsageHotspotRole::Timing, row.timing);
        addMatrixCell(row.moduleName, row.fileName, SignalUsageHotspotRole::Unknown, row.unknown);
    }
    return report;
}

bool saveVisualPreviewScreenshot()
{
    QWidget preview;
    preview.setObjectName(QStringLiteral("signalUsageHotspotPreview"));
    const int uiFontId = QFontDatabase::addApplicationFont(
        QStringLiteral("C:/Windows/Fonts/arial.ttf"));
    if (uiFontId >= 0) {
        const QStringList families =
            QFontDatabase::applicationFontFamilies(uiFontId);
        if (!families.isEmpty())
            QApplication::setFont(QFont(families.first(), 9));
    } else {
        QApplication::setFont(QFont(QStringLiteral("Arial"), 9));
    }
    auto* layout = new QHBoxLayout(&preview);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(8);

    const SignalUsageHotspotReport report = visualPreviewReport();
    auto* panel = new SignalUsageHotspotPanel(&preview);
    panel->renderReportForTest(report);
    panel->setCurrentEditorLocation(QStringLiteral("chl_ctrl.sv"), 185);
    panel->selectUsageForTest(1);
    panel->selectMatrixCellForTest(SignalUsageHotspotRole::Read,
                                   QStringLiteral("chl_ctrl"),
                                   QStringLiteral("chl_ctrl.sv"));

    layout->addWidget(panel, 1);
    preview.resize(1900, 820);
    preview.show();
    QApplication::processEvents();
    QApplication::processEvents();

    const QString outputPath =
        QDir::current().absoluteFilePath(
            QStringLiteral("current_signal_usage_hotspot_after.png"));
    const bool saved = preview.grab().save(outputPath);
    if (!saved)
        qWarning() << "Failed to save preview screenshot" << outputPath;
    else
        qInfo() << "Saved preview screenshot" << outputPath;
    return saved;
}

std::shared_ptr<SemanticIndexSnapshot> sharedSnapshotFromRecords(
    const QList<SemanticSymbolRecord>& records,
    const QList<SemanticRelationship>& relationships = {})
{
    return std::make_shared<SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(records, relationships));
}

SemanticSourceRange evidenceRange(const QString& fileName, int line)
{
    SemanticSourceRange range;
    range.fileName = fileName;
    range.line = line;
    range.column = 5;
    range.endLine = line;
    range.endColumn = 20;
    return range;
}

SignalUsageHotspotReport scopedTrackReport(int moduleEndLine,
                                           const QList<int>& usageLines)
{
    const QString fileName = QStringLiteral("scoped_module.sv");
    const SemanticSymbolRecord module =
        SemanticFixtureRecordBuilder(QStringLiteral("scope_mod"),
                                     SymbolTaxonomy::DeclarationKind::Module)
            .withFile(fileName)
            .withLocalHandle(1)
            .withRange(1, 1, moduleEndLine, 10)
            .record();
    const SemanticSymbolRecord signal =
        SemanticFixtureRecordBuilder(QStringLiteral("sig"),
                                     SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(fileName)
            .withLocalHandle(2)
            .withLine(12, 8)
            .withOwner(SymbolTaxonomy::SymbolOwnerScope::Module,
                       QStringLiteral("scope_mod"),
                       module.stableKey)
            .record();

    QList<SemanticSymbolRecord> records{module, signal};
    QList<SemanticRelationship> relationships;
    for (int i = 0; i < usageLines.size(); ++i) {
        const int line = usageLines.at(i);
        const SemanticSymbolRecord writer =
            SemanticFixtureRecordBuilder(QStringLiteral("writer_%1").arg(i),
                                         SymbolTaxonomy::DeclarationKind::Process)
                .withFile(fileName)
                .withLocalHandle(10 + i)
                .withLine(line, 1)
                .withOwner(SymbolTaxonomy::SymbolOwnerScope::Module,
                           QStringLiteral("scope_mod"),
                           module.stableKey)
                .record();
        records.append(writer);
        relationships.append(semanticFixtureRelationship(
            writer,
            signal,
            SymbolRelationshipEngine::ASSIGNS_TO,
            RelationshipProvenance::Inferred,
            100,
            QStringLiteral("sig <= value_%1;").arg(i),
            evidenceRange(fileName, line)));
    }

    SemanticIndex index;
    index.setSnapshot(sharedSnapshotFromRecords(records, relationships));
    SignalUsageHotspotService service(&index);
    SignalUsageHotspotQuery query;
    query.signalStableKey = signal.stableKey;
    query.signalName = signal.name;
    query.fileName = fileName;
    query.moduleName = QStringLiteral("scope_mod");
    return service.buildSignalUsageHotspot(query);
}

bool verifyControlledTrackGeometry(SignalUsageHotspotPanel& panel,
                                   const SignalUsageHotspotReport& report,
                                   int expectedEndLine,
                                   const QString& label)
{
    if (!report.found)
        return false;
    if (report.items.size() != 3)
        return false;
    if (report.trackLanes.isEmpty())
        return false;
    if (report.trackLanes.first().startLine != 1
        || report.trackLanes.first().endLine != expectedEndLine) {
        qWarning() << label << "lane did not keep full module scope"
                   << report.trackLanes.first().startLine
                   << report.trackLanes.first().endLine;
        return false;
    }

    panel.renderReportForTest(report);
    if (panel.firstTrackLaneStartLineForTest() != 1
        || panel.firstTrackLaneEndLineForTest() != expectedEndLine) {
        qWarning() << label << "panel lane range is not full scope"
                   << panel.firstTrackLaneStartLineForTest()
                   << panel.firstTrackLaneEndLineForTest();
        return false;
    }

    const qreal railWidth = panel.firstTrackRailWidthForTest();
    const qreal sceneWidth = panel.trackSceneWidthForTest();
    const QList<qreal> centers = panel.trackBlockCenterXsForTest();
    if (railWidth < 200.0 || railWidth > 820.0
        || sceneWidth < railWidth || sceneWidth > 1100.0) {
        qWarning() << label << "track scene or rail width is not controlled"
                   << railWidth << sceneWidth;
        return false;
    }
    if (centers.size() != 3
        || !(centers.at(0) < centers.at(1) && centers.at(1) < centers.at(2))) {
        qWarning() << label << "track blocks are not ordered" << centers;
        return false;
    }

    const qreal railLeft = 220.0;
    const qreal railRight = railLeft + railWidth;
    for (qreal center : centers) {
        if (center < railLeft - 8.0 || center > railRight + 8.0) {
            qWarning() << label << "track block is outside controlled rail"
                       << center << railLeft << railRight;
            return false;
        }
    }
    return true;
}

bool verifyReportBuildIsAsyncAndLatestWins()
{
    SignalUsageHotspotPanel panel;
    std::atomic<bool> slowStarted{false};
    panel.setReportBuilderForTest(
        [&](const SignalUsageHotspotQuery& query,
            std::shared_ptr<const SemanticIndexSnapshot>) {
            if (query.signalName == QStringLiteral("slow")) {
                slowStarted.store(true, std::memory_order_release);
                QThread::msleep(200);
            }
            SignalUsageHotspotReport report;
            report.found = true;
            report.declarationDisplayName = query.signalName;
            return report;
        });

    bool eventLoopResponsive = false;
    QTimer::singleShot(0, &panel, [&]() { eventLoopResponsive = true; });
    QElapsedTimer callTimer;
    callTimer.start();
    panel.showHotspotForSymbol(QStringLiteral("slow"),
                               QStringLiteral("slow.sv"),
                               QStringLiteral("slow_module"));
    const qint64 callElapsedMs = callTimer.elapsed();
    const bool slowRanOffThread = waitUntil(
        [&]() {
            return slowStarted.load(std::memory_order_acquire)
                && eventLoopResponsive;
        },
        1000);

    panel.showHotspotForSymbol(QStringLiteral("latest"),
                               QStringLiteral("latest.sv"),
                               QStringLiteral("latest_module"));
    const bool latestPublished = waitUntil(
        [&]() {
            return panel.currentDeclarationDisplayNameForTest()
                == QStringLiteral("latest");
        },
        1000);

    const bool allFinished = waitUntil(
        [&]() { return !panel.reportBuildInFlightForTest(); },
        1000);
    qInfo() << "Hotspot async dispatch latency ms" << callElapsedMs;
    return callElapsedMs < 50
        && slowRanOffThread
        && latestPublished
        && allFinished
        && panel.currentDeclarationDisplayNameForTest()
               == QStringLiteral("latest");
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    resetApplicationActionExecutionHistory();
    if (!verifyReportBuildIsAsyncAndLatestWins()) {
        qWarning() << "Hotspot report build blocked UI or published a stale result";
        return 1;
    }
    SignalUsageHotspotPanel panel;
    QString navigatedFile;
    int navigatedLine = -1;
    panel.setNavigationHandler(
        [&](const QString& fileName, int line, int) {
            navigatedFile = fileName;
            navigatedLine = line;
            return true;
        });
    panel.renderReportForTest(sampleReport());

    const QList<QAction*> graphViewActions =
        panel.graphViewActionsForTest();
    const auto graphActionById =
        [&graphViewActions](const char* id) {
            const QString expected =
                QString::fromLatin1(id);
            for (QAction* action : graphViewActions) {
                if (action
                    && action->property(
                           GraphExportUi::kActionIdProperty)
                           .toString()
                           == expected) {
                    return action;
                }
            }
            return static_cast<QAction*>(nullptr);
        };
    QAction* fitAction = graphActionById(
        ActionIds::GraphViewFit);
    QAction* zoomInAction = graphActionById(
        ActionIds::GraphViewZoomIn);
    QAction* zoomOutAction = graphActionById(
        ActionIds::GraphViewZoomOut);
    QAction* centerAction = graphActionById(
        ActionIds::GraphViewCenterCurrent);
    QAction* resetAction = graphActionById(
        ActionIds::GraphViewResetLayout);
    if (graphViewActions.size() != 5
        || !fitAction || !zoomInAction
        || !zoomOutAction || !centerAction
        || !resetAction
        || fitAction->text() != QStringLiteral("Fit")
        || zoomInAction->text()
               != QStringLiteral("Zoom In")
        || zoomOutAction->text()
               != QStringLiteral("Zoom Out")
        || centerAction->text()
               != QStringLiteral("Center Current")
        || resetAction->text()
               != QStringLiteral("Reset Layout")
        || fitAction->property(
               GraphExportUi::kExecutionRouteProperty)
               .toString()
               != QStringLiteral("insight.graphView.fit")
        || !fitAction->isEnabled()
        || centerAction->isEnabled()) {
        qWarning() << "Usage Hotspot graph view Actions do not match Registry metadata";
        return 1;
    }
    QPushButton* zoomInButton =
        panel.findChild<QPushButton*>(
            QStringLiteral(
                "signalUsageHotspotZoomInButton"));
    if (!zoomInButton
        || zoomInButton->text()
               != zoomInAction->text()
        || zoomInButton->property(
               GraphExportUi::kActionIdProperty)
               .toString()
               != QString::fromLatin1(
                   ActionIds::GraphViewZoomIn)) {
        qWarning() << "Usage Hotspot toolbar does not consume Registry Action metadata";
        return 1;
    }
    const ActionExecutionResult resetResult =
        panel.triggerGraphViewActionForTest(
            QString::fromLatin1(
                ActionIds::GraphViewResetLayout));
    if (!resetResult.succeeded
        || qAbs(panel.trackZoomFactorForTest() - 1.0)
               > 0.001) {
        qWarning() << "Registered Reset Layout Action failed";
        return 1;
    }
    panel.focusZoomIn();
    if (panel.trackZoomFactorForTest() <= 1.0) {
        qWarning() << "Registered Focus Zoom In Action failed";
        return 1;
    }
    const ActionExecutionResult zoomOutResult =
        panel.triggerGraphViewActionForTest(
            QString::fromLatin1(
                ActionIds::GraphViewZoomOut));
    const ActionExecutionResult fitResult =
        panel.triggerGraphViewActionForTest(
            QString::fromLatin1(
                ActionIds::GraphViewFit));
    if (!zoomOutResult.succeeded
        || !fitResult.succeeded
        || applicationActionExecutionHistory()
               .hasRepeatableAction()) {
        qWarning() << "Registered graph view Actions failed or displaced repeat history";
        return 1;
    }

    if (panel.trackBlockCountForTest() != 2) {
        qWarning() << "Expected 2 track blocks, got"
                   << panel.trackBlockCountForTest();
        return 1;
    }
    if (panel.matrixNonEmptyCellCountForTest() != 2) {
        qWarning() << "Expected 2 matrix cells, got"
                   << panel.matrixNonEmptyCellCountForTest();
        return 1;
    }
    if (panel.matrixItemCountForTest() != 2) {
        qWarning() << "Expected 2 matrix usage rows, got"
                   << panel.matrixItemCountForTest();
        return 1;
    }
    if (!panel.selectMatrixCellForTest(SignalUsageHotspotRole::Write,
                                       QStringLiteral("chl_ctrl"),
                                       QStringLiteral("chl_ctrl.sv"))) {
        qWarning() << "Matrix cell selection did not focus any usage";
        return 1;
    }
    if (panel.trackBlockCountForTest() != 2
        || panel.matrixItemCountForTest() != 1) {
        qWarning() << "Matrix-to-track focus failed"
                   << panel.trackBlockCountForTest()
                   << panel.matrixItemCountForTest();
        return 1;
    }
    panel.setCurrentEditorLocation(QStringLiteral("chl_ctrl.sv"), 18);
    centerAction = panel.graphViewActionForTest(
        QString::fromLatin1(
            ActionIds::GraphViewCenterCurrent));
    const ActionExecutionResult centerResult =
        panel.triggerGraphViewActionForTest(
            QString::fromLatin1(
                ActionIds::GraphViewCenterCurrent));
    if (!centerAction || !centerAction->isEnabled()
        || !centerResult.succeeded) {
        qWarning() << "Registered Center Current Action failed";
        return 1;
    }
    if (panel.trackBlockCountForTest() != 2) {
        qWarning() << "Current-line marker changed focused track count";
        return 1;
    }

    const SignalUsageHotspotReport previewReport = visualPreviewReport();
    panel.renderReportForTest(previewReport);
    const int fullPreviewLaneCount = panel.trackLaneCountForTest();
    const int fullPreviewBlockCount = panel.trackBlockCountForTest();
    if (fullPreviewLaneCount != 4 || fullPreviewBlockCount < 18) {
        qWarning() << "Preview report did not render expected dense lanes"
                   << fullPreviewLaneCount << fullPreviewBlockCount;
        return 1;
    }
    if (!panel.selectMatrixCellForTest(SignalUsageHotspotRole::Read,
                                       QStringLiteral("chl_ctrl"),
                                       QStringLiteral("chl_ctrl.sv"))) {
        qWarning() << "Dense matrix cell did not focus usage";
        return 1;
    }
    if (panel.trackLaneCountForTest() != fullPreviewLaneCount
        || panel.trackBlockCountForTest() != fullPreviewBlockCount
        || panel.matrixItemCountForTest() != 12) {
        qWarning() << "Matrix focus should highlight without hiding track lanes"
                   << panel.trackLaneCountForTest()
                   << fullPreviewLaneCount
                   << panel.trackBlockCountForTest()
                   << fullPreviewBlockCount
                   << panel.matrixItemCountForTest();
        return 1;
    }
    if (!panel.triggerFirstUsageNavigationForTest()
        || navigatedFile != QStringLiteral("chl_ctrl.sv")
        || navigatedLine != 112) {
        qWarning() << "Navigation hook failed" << navigatedFile << navigatedLine;
        return 1;
    }

    const SignalUsageHotspotReport scoped200Report =
        scopedTrackReport(200, {20, 100, 180});
    if (!verifyControlledTrackGeometry(panel,
                                       scoped200Report,
                                       200,
                                       QStringLiteral("scope 200"))) {
        return 1;
    }
    const qreal scope200RailWidth = panel.firstTrackRailWidthForTest();
    const qreal scope200SceneWidth = panel.trackSceneWidthForTest();

    const SignalUsageHotspotReport scoped2000Report =
        scopedTrackReport(2000, {200, 1000, 1800});
    if (!verifyControlledTrackGeometry(panel,
                                       scoped2000Report,
                                       2000,
                                       QStringLiteral("scope 2000"))) {
        return 1;
    }
    if (qAbs(panel.firstTrackRailWidthForTest() - scope200RailWidth) > 1.0
        || qAbs(panel.trackSceneWidthForTest() - scope200SceneWidth) > 1.0) {
        qWarning() << "Track width grew with line span"
                   << scope200RailWidth
                   << panel.firstTrackRailWidthForTest()
                   << scope200SceneWidth
                   << panel.trackSceneWidthForTest();
        return 1;
    }
    if (!saveVisualPreviewScreenshot())
        return 1;
    return 0;
}
