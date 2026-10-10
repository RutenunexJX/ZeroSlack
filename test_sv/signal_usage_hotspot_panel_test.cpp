#include "actionregistry.h"
#include "testuistyle.h"
#include "graphexportui.h"
#include "signalusagehotspotpanel.h"
#include "semantic_fixture_records.h"
#include "semanticindexsnapshot.h"

#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFont>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QPushButton>
#include <QToolButton>
#include <QSettings>
#include <QMenu>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QGraphicsSimpleTextItem>
#include <QScrollBar>
#include <QScopeGuard>
#include <QtTest>
#include <QPixmap>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <cstdio>
#include <functional>
#include <memory>

namespace {
void stderrQtMessageHandler(QtMsgType type,
                            const QMessageLogContext&,
                            const QString& message)
{
    const char* level = "INFO";
    if (type == QtWarningMsg)
        level = "WARN";
    else if (type == QtCriticalMsg || type == QtFatalMsg)
        level = "ERROR";
    const QByteArray encoded = message.toLocal8Bit();
    std::fprintf(stderr, "[%s] %s\n", level, encoded.constData());
    std::fflush(stderr);
}

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
    if (railWidth < 180.0
        || qAbs(sceneWidth - railWidth - panel.trackRailLeftForTest() - 24.0) > 0.01) {
        qWarning() << label << "track scene or rail width is not controlled"
                   << railWidth << sceneWidth;
        return false;
    }
    if (centers.size() != 3
        || !(centers.at(0) < centers.at(1) && centers.at(1) < centers.at(2))) {
        qWarning() << label << "track blocks are not ordered" << centers;
        return false;
    }

    const qreal railLeft = panel.trackRailLeftForTest();
    const qreal railRight = railLeft + railWidth;
    const QList<qreal> fractions{0.1, 0.5, 0.9};
    for (int i = 0; i < centers.size(); ++i) {
        const qreal center = centers.at(i);
        if (center < railLeft - 8.0 || center > railRight + 8.0) {
            qWarning() << label << "track block is outside controlled rail"
                       << center << railLeft << railRight;
            return false;
        }
        const qreal expected = railLeft + railWidth * (qRound(expectedEndLine * fractions[i]) - 1) / (expectedEndLine - 1);
        if (qAbs(center - expected) > 0.01) return false;
    }
    return true;
}

bool verifyAllRolesVisible()
{
    SignalUsageHotspotPanel panel;
    if (!panel.findChildren<QCheckBox*>().isEmpty()) {
        qWarning() << "Hotspot type filter checkboxes must be removed";
        return false;
    }
    auto allRoles = sampleReport();
    allRoles.items.clear(); allRoles.matrixCells.clear(); allRoles.trackLanes.first().positions.clear();
    const QList<SignalUsageHotspotRole> roles{SignalUsageHotspotRole::Write, SignalUsageHotspotRole::Read,
        SignalUsageHotspotRole::Port, SignalUsageHotspotRole::Condition, SignalUsageHotspotRole::Case,
        SignalUsageHotspotRole::Timing, SignalUsageHotspotRole::Unknown};
    for (const auto role : roles) {
        const int index = allRoles.items.size();
        const auto name = SignalUsageHotspotService::roleDisplayName(role);
        allRoles.items.append(item(role, "chl_ctrl", "chl_ctrl.sv", 18 + index * 3, name));
        SignalUsageHotspotTrackPosition position;
        position.itemIndex = index; position.role = role; position.roleDisplayName = name;
        position.line = 18 + index * 3; position.column = 3;
        allRoles.trackLanes.first().positions.append(position);
        SignalUsageHotspotMatrixCell cell;
        cell.moduleName = "chl_ctrl"; cell.fileName = "chl_ctrl.sv";
        cell.role = role; cell.roleDisplayName = name; cell.count = 1;
        allRoles.matrixCells.append(cell);
    }
    allRoles.trackLanes.first().count = roles.size();
    panel.renderReportForTest(allRoles);
    int retained = 0;
    for (const auto& cluster : panel.trackClustersForTest()) retained += cluster.size();
    if (retained != 7 || panel.matrixNonEmptyCellCountForTest() != 7) {
        qWarning() << "All seven roles must remain visible without type filters";
        return false;
    }
    panel.setFocusSearchText("Unknown");
    if (panel.trackBlockCountForTest() != 1) return false;
    panel.setFocusSearchText("");
    retained = 0;
    for (const auto& cluster : panel.trackClustersForTest()) retained += cluster.size();
    if (retained != 7) return false;
    for (int role = 0; role < 7; ++role) {
        auto* button = panel.findChild<QToolButton*>(QStringLiteral("hotspotRole_%1").arg(role));
        if (!button || !button->isChecked()) return false;
        button->click();
        if (panel.matrixItemCountForTest() != 6) return false;
        button->click();
        if (panel.matrixItemCountForTest() != 7) return false;
    }
    const auto evidence = qEnvironmentVariable("ZEROSLACK_UI_EVIDENCE_DIR");
    if (!evidence.isEmpty()) {
        QDir().mkpath(evidence);
        panel.setMatrixModeForTest(false);
        panel.resize(1400, 360); panel.show(); QApplication::processEvents();
        panel.grab().save(evidence + "/hotspot-all-roles-track.png");
        panel.setMatrixModeForTest(true); QApplication::processEvents();
        panel.grab().save(evidence + "/hotspot-all-roles-matrix.png");
    }
    return true;
}

bool verifyDenseClustersAndReadingPosition()
{
    SignalUsageHotspotPanel panel;
    panel.resize(960, 320); panel.show(); QApplication::processEvents();
    auto report = scopedTrackReport(2000, QList<int>(120, 1000));
    panel.renderReportForTest(report); panel.setMatrixModeForTest(false);
    if (panel.trackClustersForTest().size() != 1 || panel.trackClustersForTest().first().size() != 120) return false;
    if (panel.rowHeightForTest() < 40 || panel.rowHeightForTest() > 55) return false;
    if (!panel.selectTrackClusterForTest(0)) return false;
    auto* list = panel.inlineItemsForTest();
    if (!list || list->topLevelItemCount() != 120) return false;
    QString jumpedFile; int jumpedLine = 0, jumpedColumn = 0;
    panel.setNavigationHandler([&](const QString& file, int line, int column) {
        jumpedFile = file; jumpedLine = line; jumpedColumn = column; return true;
    });
    list->setCurrentItem(list->topLevelItem(119)); list->setFocus();
    QTest::keyClick(list, Qt::Key_Return);
    if (jumpedFile != QStringLiteral("scoped_module.sv") || jumpedLine != 1000 || jumpedColumn != 5) return false;
    if (panel.selectedItemIndexForTest() != 119) return false;
    panel.setMatrixModeForTest(true); panel.setMatrixModeForTest(false);
    if (panel.selectedItemIndexForTest() != 119 || !panel.inlineItemsForTest()
        || panel.inlineItemsForTest()->topLevelItemCount() != 120) return false;
    panel.collapseDetails();
    if (panel.inlineItemsForTest()) return false;
    const qreal width = panel.firstTrackRailWidthForTest();
    panel.focusZoomIn();
    auto* view = panel.findChild<QGraphicsView*>(QStringLiteral("signalUsageHotspotTrackView"));
    if (panel.firstTrackRailWidthForTest() <= width || !view || view->transform() != QTransform()) return false;
    panel.setMatrixModeForTest(true);
    panel.selectMatrixCellForTest(SignalUsageHotspotRole::Write, "scope_mod", "scoped_module.sv");
    const auto selected = panel.selectedItemIndexForTest();
    panel.setFocusSearchText("sig"); panel.setMatrixModeForTest(false);
    if (panel.selectedItemIndexForTest() != selected || panel.focusSearchText() != "sig" || panel.matrixItemCountForTest() != 120) return false;
    auto rows = sampleReport();
    const auto source = rows;
    for (int row = 1; row < 30; ++row) {
        auto lane = source.trackLanes.first();
        lane.moduleName = QStringLiteral("module_%1").arg(row);
        lane.positions.clear();
        for (auto usage : source.items) {
            usage.moduleName = lane.moduleName;
            rows.items.append(usage);
        }
        rows.trackLanes.append(lane);
    }
    panel.setFocusSearchText(""); panel.renderReportForTest(rows);
    QApplication::processEvents();
    view->verticalScrollBar()->setValue(470);
    const int scroll = view->verticalScrollBar()->value();
    panel.setMatrixModeForTest(true);
    auto* matrix = panel.findChild<QGraphicsView*>("signalUsageHotspotMatrixView");
    if (!matrix || qAbs(matrix->verticalScrollBar()->value() - scroll) > 1) return false;
    panel.setMatrixModeForTest(false);
    if (qAbs(view->verticalScrollBar()->value() - scroll) > 1) return false;
    panel.renderReportForTest(scopedTrackReport(2000, {1000}));
    QApplication::processEvents();
    QGraphicsItem* single = nullptr;
    for (auto* mark : view->scene()->items())
        if (mark->toolTip().contains("1000:5") && mark->toolTip().contains("scoped_module.sv")) single = mark;
    if (!single) return false;
    jumpedLine = 0;
    const QPoint point = view->mapFromScene(single->sceneBoundingRect().center());
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, point);
    QApplication::processEvents();
    QTest::mouseDClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, point);
    if (jumpedLine != 1000 || jumpedColumn != 5) return false;
    panel.setWorkspaceRoot("changed-workspace");
    return panel.trackClustersForTest().isEmpty() && panel.currentDeclarationDisplayNameForTest().isEmpty();
}

bool verifySnapshotReplacementAndStateRestore()
{
    auto* index = SemanticIndex::getInstance();
    const auto previous = index->snapshot();
    const auto restore = qScopeGuard([&] { index->setSnapshot(previous); });
    index->setSnapshot(sharedSnapshotFromRecords({}));
    SignalUsageHotspotPanel panel;
    panel.setReportBuilderForTest([](const SignalUsageHotspotQuery&, std::shared_ptr<const SemanticIndexSnapshot> snapshot) {
        auto report = sampleReport();
        if (snapshot->getSymbolRecordsByName("updated").isEmpty()) {
            QThread::msleep(80); report.declarationDisplayName = "obsolete";
        } else report.declarationDisplayName = "updated";
        return report;
    });
    panel.showHotspotForSymbol("mcs", "chl_ctrl.sv", "chl_ctrl");
    const auto record = SemanticFixtureRecordBuilder("updated", SymbolTaxonomy::DeclarationKind::Module)
        .withFile("chl_ctrl.sv").withLocalHandle(1).withLine(1, 1).record();
    index->setSnapshot(sharedSnapshotFromRecords({record}));
    bool obsoletePublished = false;
    if (!waitUntil([&] {
        obsoletePublished |= panel.currentDeclarationDisplayNameForTest() == "obsolete";
        return !panel.reportBuildInFlightForTest();
    }, 1500) || obsoletePublished || panel.currentDeclarationDisplayNameForTest() != "updated"
        || panel.reportBuildRequestCountForTest() != 2) return false;
    panel.selectUsageForTest(1);
    panel.setFocusSearchText("mcs"); panel.setMatrixModeForTest(false);
    const auto state = panel.saveViewState();
    panel.setReportBuilderForTest([](const SignalUsageHotspotQuery&, std::shared_ptr<const SemanticIndexSnapshot>) {
        auto report = sampleReport();
        report.items.prepend(item(SignalUsageHotspotRole::Unknown, "chl_ctrl", "chl_ctrl.sv", 20, "new mcs usage"));
        return report;
    });
    panel.refreshReport();
    if (!waitUntil([&] { return !panel.reportBuildInFlightForTest(); }, 1500)
        || panel.selectedItemIndexForTest() != 2) return false;
    panel.setFocusSearchText("missing"); panel.restoreViewState(state);
    if (!waitUntil([&] { return !panel.reportBuildInFlightForTest(); }, 1500)) return false;
    return panel.selectedItemIndexForTest() == 2 && panel.focusSearchText() == "mcs"
        && !panel.matrixModeForTest() && panel.inlineItemsForTest();
}

bool verifyLatestEmptyStateWhileOlderRequestRuns()
{
    SignalUsageHotspotPanel panel;
    const auto emptyState = panel.saveViewState();
    auto releaseOld = std::make_shared<std::atomic<bool>>(false);
    const auto release = qScopeGuard([releaseOld] { releaseOld->store(true); });
    panel.setReportBuilderForTest([releaseOld](const SignalUsageHotspotQuery& query,
        std::shared_ptr<const SemanticIndexSnapshot>) {
        if (query.signalName == "slow") while (!releaseOld->load()) QThread::msleep(1);
        SignalUsageHotspotReport report;
        report.notFoundReasonDisplayName = "Target unavailable";
        return report;
    });
    panel.showHotspotForSymbol("slow", "source.sv", "module");
    panel.showHotspotForSymbol("missing", "source.sv", "module");
    auto* view = panel.findChild<QGraphicsView*>("signalUsageHotspotMatrixView");
    const bool latestShown = waitUntil([&] {
        for (auto* item : view->scene()->items())
            if (auto* text = qgraphicsitem_cast<QGraphicsSimpleTextItem*>(item))
                if (text->text() == "Target unavailable") return true;
        return false;
    }, 1000);
    const bool olderStillRunning = panel.reportBuildInFlightForTest();
    releaseOld->store(true);
    if (!waitUntil([&] { return !panel.reportBuildInFlightForTest(); }, 1000)
        || !latestShown || !olderStillRunning) return false;
    panel.restoreViewState(emptyState);
    const auto requests = panel.reportBuildRequestCountForTest();
    panel.refreshReport();
    return panel.saveViewState().value("signal").toString().isEmpty()
        && panel.reportBuildRequestCountForTest() == requests && panel.trackClustersForTest().isEmpty();
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
    const bool latestWins = callElapsedMs < 50
        && slowRanOffThread
        && latestPublished
        && allFinished
        && panel.currentDeclarationDisplayNameForTest()
               == QStringLiteral("latest");
    panel.showHotspotForSymbol("slow", "slow.sv", "slow_module");
    panel.setWorkspaceRoot("another-workspace");
    const bool cleared = waitUntil([&] { return !panel.reportBuildInFlightForTest(); }, 1000)
        && panel.currentDeclarationDisplayNameForTest().isEmpty();
    return latestWins && cleared;
}
}

int main(int argc, char** argv)
{
    qInstallMessageHandler(stderrQtMessageHandler);
    QApplication app(argc, argv);
    QTemporaryDir profile;
    QCoreApplication::setOrganizationName("ZeroSlackHotspotTest");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, profile.path());
    if (!initializeUiStyleForTest()) return 3;
    { SignalUsageHotspotPanel defaults; if (!defaults.matrixModeForTest()) return 1; }
    resetApplicationActionExecutionHistory();
    if (!verifyAllRolesVisible()) return 1;
    if (!verifyDenseClustersAndReadingPosition()) { qWarning() << "Dense cluster, inline navigation, role filter or axis zoom contract failed"; return 1; }
    if (!verifySnapshotReplacementAndStateRestore()) { qWarning() << "Snapshot replacement or stable selection restore failed"; return 1; }
    if (!verifyLatestEmptyStateWhileOlderRequestRuns()) { qWarning() << "Latest empty-query status or empty target restore failed"; return 1; }
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

    QPushButton* trackModeButton =
        panel.findChild<QPushButton*>(
            QStringLiteral("signalUsageHotspotTrackModeButton"));
    QPushButton* matrixModeButton =
        panel.findChild<QPushButton*>(
            QStringLiteral("signalUsageHotspotMatrixModeButton"));
    if (!trackModeButton || !matrixModeButton
        || !trackModeButton->isVisibleTo(&panel)
        || !matrixModeButton->isVisibleTo(&panel)) {
        qWarning() << "Hotspot view switch is not available";
        return 1;
    }
    panel.setMatrixModeForTest(true);
    if (!panel.matrixModeForTest()
        || !matrixModeButton->isChecked()
        || trackModeButton->isChecked()) {
        qWarning() << "Hotspot Matrix view did not activate";
        return 1;
    }
    panel.setMatrixModeForTest(false);
    if (panel.matrixModeForTest()
        || !trackModeButton->isChecked()
        || matrixModeButton->isChecked()) {
        qWarning() << "Hotspot Track view did not reactivate";
        return 1;
    }

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
    auto* more = panel.findChild<QToolButton*>(QStringLiteral("signalUsageHotspotMoreButton"));
    if (!more || !more->menu() || !more->menu()->actions().contains(zoomInAction)) {
        qWarning() << "Usage Hotspot More menu does not consume Registry Action metadata";
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
    int referenceCount = 0;
    for (const auto& cluster : panel.trackClustersForTest()) referenceCount += cluster.size();
    if (fullPreviewLaneCount != 4 || referenceCount != previewReport.items.size()) {
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
