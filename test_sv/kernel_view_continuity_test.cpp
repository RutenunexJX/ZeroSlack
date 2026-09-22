#include "applicationthememanager.h"
#include "testuistyle.h"
#include "signalkernelgraphpanelcoordinator.h"
#include "rtlinsightworkbench.h"
#include "projectmodel.h"
#include "relationshipanalysisworker.h"
#include "semanticindexsnapshot.h"
#include "slangmanager.h"
#include "symbolanalyzer.h"

#include <QApplication>
#include <QCheckBox>
#include <QElapsedTimer>
#include <QDirIterator>
#include <QEventLoop>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QLineF>
#include <QMainWindow>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <iostream>

namespace {
int checks = 0;
int failures = 0;

void check(bool value, const char* description)
{
    ++checks;
    if (!value) {
        ++failures;
        std::cerr << "FAIL: " << description << '\n';
    }
}

SignalKernelGraphNode node(int id, const QString& name,
                           SignalKernelGraphNodeRole role)
{
    SignalKernelGraphNode result;
    result.id = id;
    result.displayName = name;
    result.role = role;
    result.moduleDisplayName = QStringLiteral("continuity_top");
    result.stableKey.fileName = QStringLiteral("continuity_fixture.sv");
    result.stableKey.symbolName = name;
    result.stableKey.ownerScope = result.moduleDisplayName;
    result.stableKey.declarationKind = SymbolTaxonomy::DeclarationKind::Port;
    result.stableKey.sourcePosition = id * 20;
    result.stableKey.sourceLength = name.size();
    return result;
}

SignalKernelGraphReport fixture(int outputCount = 12)
{
    SignalKernelGraphReport report;
    report.found = true;
    report.kernelModuleName = QStringLiteral("continuity_top");
    report.kernel = node(0, QStringLiteral("tracked"), SignalKernelGraphNodeRole::Kernel);
    for (int i = 0; i < 6; ++i) {
        auto input = node(i + 1, QStringLiteral("input_%1").arg(i),
                          SignalKernelGraphNodeRole::Input);
        input.crossModule = i % 2 == 0;
        input.inputLane = static_cast<SignalKernelGraphInputLane>(i % 3);
        report.inputs.append(input);
        report.edges.append({input.id, 0, QStringLiteral("read")});
    }
    for (int i = 0; i < outputCount; ++i) {
        auto output = node(i + 10, QStringLiteral("output_%1").arg(i),
                           SignalKernelGraphNodeRole::Output);
        output.crossModule = i % 2 == 0;
        report.outputs.append(output);
        report.edges.append({0, output.id, QStringLiteral("write")});
    }
    SignalKernelGraphFanoutGroup group;
    group.id = 1;
    group.groupKey = QStringLiteral("outputs:continuity_top");
    group.moduleName = report.kernelModuleName;
    group.displayName = QStringLiteral("Grouped outputs");
    group.highFanout = true;
    for (const auto& output : report.outputs)
        group.nodeIds.append(output.id);
    group.nodeCount = group.totalRoleNodeCount = outputCount;
    report.outputFanoutGroups.append(group);
    return report;
}

QGraphicsItem* labelledItem(QGraphicsView* view, const QString& text)
{
    for (auto* item : view->scene()->items()) {
        auto* label = dynamic_cast<QGraphicsSimpleTextItem*>(item);
        if (label && label->text() == text && label->parentItem()
            && label->parentItem()->flags().testFlag(QGraphicsItem::ItemIsSelectable))
            return label->parentItem();
    }
    return nullptr;
}

bool selected(QGraphicsView* view, const QString& text)
{
    auto* item = labelledItem(view, text);
    return item && item->isSelected();
}

struct ViewState {
    QTransform transform;
    QPointF center;
};

ViewState capture(QGraphicsView* view)
{
    return {view->transform(), view->mapToScene(view->viewport()->rect().center())};
}

qreal centerError(QGraphicsView* view, const ViewState& before)
{
    const auto after = capture(view);
    return QLineF(before.transform.map(before.center),
                  before.transform.map(after.center)).length();
}

void pose(QGraphicsView* view, qreal zoom = 1.7, QPointF center = QPointF(10, 25))
{
    view->setTransform(QTransform::fromScale(zoom, zoom));
    view->centerOn(center);
    QApplication::processEvents();
}

void verifyPose(QGraphicsView* view, const ViewState& before, const char* description)
{
    check(view->transform() == before.transform && centerError(view, before) <= 2.0,
          description);
}

int measureWorkspace(const QString& path)
{
    QStringList files;
    QDirIterator it(path, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) files.append(it.next());
    files.sort();
    ProjectModel projectModel;
    projectModel.setWorkspaceRoot(path);
    projectModel.setScannedFiles(files);
    const auto project = projectModel.snapshot();
    if (project.systemVerilogFiles.isEmpty()) return 2;
    SymbolAnalyzer analyzer;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool completed = false;
    QObject::connect(&analyzer, &SymbolAnalyzer::analysisCompleted, &loop,
                     [&](const QString& file, int) {
                         if (QDir::cleanPath(file) == QDir::cleanPath(project.workspaceRoot)) {
                             completed = true;
                             loop.quit();
                         }
                     });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(120000);
    analyzer.startAnalyzeProjectAsync(project);
    loop.exec();
    if (!completed) return 3;
    auto* index = SemanticIndex::getInstance();
    const auto base = index->beginRelationshipAnalysisSnapshot();
    SlangManager slang;
    SymbolRelationshipEngine engine;
    auto builder = index->createRelationshipBuilder(&engine, &slang);
    const auto relationships = RelationshipAnalysisWorker::analyzeWorkspace(builder.get(), project, base);
    if (relationships.cancelled || !relationships.semanticSnapshot) return 4;
    index->setSnapshot(relationships.semanticSnapshot);
    QHash<int, int> degree;
    for (const auto& relation : relationships.semanticSnapshot->relationshipsView()) {
        ++degree[relation.fromId];
        ++degree[relation.toId];
    }
    auto records = relationships.semanticSnapshot->getSymbolRecords();
    std::stable_sort(records.begin(), records.end(), [&](const auto& a, const auto& b) {
        return degree.value(a.localHandle) > degree.value(b.localHandle);
    });
    SignalKernelGraphService service(index);
    SignalKernelGraphReport report;
    SignalKernelGraphQuery query;
    for (int i = 0; i < qMin(32, records.size()); ++i) {
        SignalKernelGraphQuery candidate;
        candidate.signalStableKey = records[i].stableKey;
        candidate.signalName = records[i].name;
        candidate.fileName = records[i].location.fileName;
        candidate.moduleName = records[i].owner.name;
        const auto graph = service.buildSignalKernelGraph(candidate);
        if (graph.found && graph.inputs.size() + graph.outputs.size()
                > report.inputs.size() + report.outputs.size()) {
            report = graph;
            query = candidate;
        }
    }
    if (!report.found || report.inputs.size() + report.outputs.size() < 2) return 5;
    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < 10; ++i) service.buildSignalKernelGraph(query);
    const double reportMs = timer.nsecsElapsed() / 1e6 / 10;
    QMainWindow host;
    SignalKernelGraphPanelCoordinator panel(&host);
    host.addDockWidget(Qt::RightDockWidgetArea, panel.dock());
    host.resize(1060, 760);
    panel.dock()->show();
    host.show();
    QApplication::processEvents();
    panel.renderReportForTest(report);
    for (const auto& group : report.inputFanoutGroups)
        if (group.highFanout) panel.toggleFanoutGroupForTest(group.groupKey);
    for (const auto& group : report.outputFanoutGroups)
        if (group.highFanout) panel.toggleFanoutGroupForTest(group.groupKey);
    QApplication::processEvents();
    auto* view = panel.view();
    auto* search = host.findChild<QLineEdit*>(QStringLiteral("signalKernelGraphSearchEdit"));
    pose(view);
    const auto before = capture(view);
    timer.restart();
    for (int i = 0; i < 30; ++i)
        search->setText(i % 2 ? report.kernel.displayName : QString());
    const double rebuildMs = timer.nsecsElapsed() / 1e6 / 30;
    const double drift = centerError(view, before);
    const bool zoomPreserved = view->transform() == before.transform;
    pose(view);
    QImage image(view->viewport()->size() * view->devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(view->devicePixelRatioF());
    timer.restart();
    for (int i = 0; i < 30; ++i) {
        image.fill(Qt::transparent);
        view->viewport()->render(&image);
    }
    const QJsonObject metrics{
        {"scale", view->devicePixelRatioF()},
        {"workspace_files", project.systemVerilogFiles.size()},
        {"nodes", 1 + report.inputs.size() + report.outputs.size()},
        {"groups", report.inputFanoutGroups.size() + report.outputFanoutGroups.size()},
        {"expanded_visible_nodes", panel.visibleGraphNodeCountForTest()},
        {"report_mean_ms", reportMs},
        {"search_rebuild_mean_ms", rebuildMs},
        {"viewport_paint_mean_ms", timer.nsecsElapsed() / 1e6 / 30},
        {"search_center_error_px", drift},
        {"search_transform_preserved", zoomPreserved}
    };
    std::cout << "WORKSPACE_METRICS " << QJsonDocument(metrics).toJson(QJsonDocument::Compact).constData() << '\n';
    const QString output = qEnvironmentVariable("ZEROSLACK_KERNEL_REVIEW_DIR");
    if (!output.isEmpty()) {
        QDir().mkpath(output);
        host.grab().save(QDir(output).filePath(QStringLiteral("real-kernel-%1.png").arg(view->devicePixelRatioF())));
    }
    return 0;
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    if (!initializeUiStyleForTest()) return 3;
    const QString workspace = qEnvironmentVariable("ZEROSLACK_KERNEL_WORKSPACE");
    if (!workspace.isEmpty()) return measureWorkspace(workspace);
    const bool measureOnly = app.arguments().contains(QStringLiteral("--measure-only"));
    ApplicationThemeManager::instance().setMode(ThemeMode::Light);
    QMainWindow host;
    SignalKernelGraphPanelCoordinator panel(&host);
    host.addDockWidget(Qt::RightDockWidgetArea, panel.dock());
    host.resize(1060, 760);
    panel.dock()->show();
    host.show();
    QApplication::processEvents();
    auto report = fixture();
    panel.renderReportForTest(report);
    QApplication::processEvents();
    auto* view = panel.view();
    auto* search = host.findChild<QLineEdit*>(QStringLiteral("signalKernelGraphSearchEdit"));
    auto* inputs = host.findChild<QCheckBox*>(QStringLiteral("signalKernelGraphShowInputsCheck"));
    auto* outputs = host.findChild<QCheckBox*>(QStringLiteral("signalKernelGraphShowOutputsCheck"));
    auto* cross = host.findChild<QCheckBox*>(QStringLiteral("signalKernelGraphCrossModuleOnlyCheck"));
    if (!view || !search || !inputs || !outputs || !cross)
        return 2;

    qreal maxThemeError = 0;
    bool themeTransformPreserved = true;
    for (qreal zoom : {0.9, 1.7, 2.4}) {
        for (const QPointF center : {QPointF(0, 0), QPointF(35, 61), QPointF(-45, -72)}) {
            pose(view, zoom, center);
            const auto original = capture(view);
            for (int i = 0; i < 12; ++i) {
                ApplicationThemeManager::instance().setMode(i % 2 ? ThemeMode::Light : ThemeMode::Dark);
                panel.refreshThemePresentation();
                QApplication::processEvents();
                maxThemeError = qMax(maxThemeError, centerError(view, original));
                themeTransformPreserved &= view->transform() == original.transform;
            }
        }
    }
    pose(view);
    auto* kernel = labelledItem(view, QStringLiteral("tracked"));
    if (!kernel)
        return 2;
    kernel->setSelected(true);
    const auto beforeSearch = capture(view);
    search->setText(QStringLiteral("output_"));
    const qreal searchScaleBefore = beforeSearch.transform.m11();
    const qreal searchScaleAfter = view->transform().m11();
    const bool searchSelection = selected(view, QStringLiteral("tracked"));
    const qreal searchCenterError = centerError(view, beforeSearch);

    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < 30; ++i)
        search->setText(i % 2 ? QStringLiteral("output_") : QStringLiteral("input_"));
    const double searchRenderMs = timer.nsecsElapsed() / 1e6 / 30;
    QImage image(view->viewport()->size() * view->devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(view->devicePixelRatioF());
    timer.restart();
    for (int i = 0; i < 30; ++i) {
        image.fill(Qt::transparent);
        view->viewport()->render(&image);
    }
    const double paintMs = timer.nsecsElapsed() / 1e6 / 30;
    const QJsonObject metrics{
        {"scale", view->devicePixelRatioF()},
        {"theme_round_trips", 108},
        {"theme_max_cumulative_center_error_px", maxThemeError},
        {"theme_transform_preserved", themeTransformPreserved},
        {"search_scale_before", searchScaleBefore},
        {"search_scale_after", searchScaleAfter},
        {"search_center_error_px", searchCenterError},
        {"search_selection_preserved", searchSelection},
        {"search_rebuild_mean_ms", searchRenderMs},
        {"viewport_paint_mean_ms", paintMs}
    };
    std::cout << "METRICS " << QJsonDocument(metrics).toJson(QJsonDocument::Compact).constData() << '\n';
    if (measureOnly)
        return 0;

    check(themeTransformPreserved, "theme refresh preserves the transform repeatedly");
    check(maxThemeError <= 2.0, "theme refresh does not accumulate viewport drift");
    check(searchScaleBefore == searchScaleAfter, "search text changes preserve zoom");
    check(searchCenterError <= 2.0, "search text changes preserve center");
    check(searchSelection, "search text changes preserve selection");
    search->clear();
    pose(view, 2.4, QPointF(230, -150));
    labelledItem(view, QStringLiteral("tracked"))->setSelected(true);
    const auto beforeInput = capture(view);
    QTest::mouseClick(inputs, Qt::LeftButton);
    verifyPose(view, beforeInput, "input toggle preserves viewport");
    check(selected(view, QStringLiteral("tracked")), "filter preserves surviving selection");
    QTest::mouseClick(inputs, Qt::LeftButton);
    pose(view, 1.7, QPointF(-400, 0));
    const auto beforeClamp = capture(view);
    QTest::mouseClick(inputs, Qt::LeftButton);
    check(view->transform() == beforeClamp.transform
              && view->horizontalScrollBar()->value() == view->horizontalScrollBar()->minimum(),
          "filter clamps an unreachable center to the nearest boundary without zooming");
    QTest::mouseClick(inputs, Qt::LeftButton);
    QTest::mouseClick(cross, Qt::LeftButton);
    QTest::mouseClick(cross, Qt::LeftButton);

    search->setText(QStringLiteral("output_"));
    pose(view);
    const auto beforeLocate = capture(view);
    QTest::keyClick(search, Qt::Key_Return);
    check(view->transform() == beforeLocate.transform, "Enter search preserves zoom");
    check(centerError(view, beforeLocate) > 2.0, "Enter locates a matching group without expanding it");
    check(panel.collapsedFanoutGroupCountForTest() == 1, "search locate keeps collapse state");
    search->setText(QStringLiteral("absent-symbol"));
    const auto noMatch = capture(view);
    QTest::keyClick(search, Qt::Key_Return);
    verifyPose(view, noMatch, "no-match Enter leaves viewport unchanged");

    search->setText(QStringLiteral("output_"));
    panel.focusFit();
    QApplication::processEvents();
    const auto beforeGroup = capture(view);
    const QPoint groupPoint = view->mapFromScene(panel.lastRenderedFanoutGroupRectForTest().center());
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, groupPoint);
    check(panel.collapsedFanoutGroupCountForTest() == 0, "actual group click expands the fanout group");
    check(selected(view, QStringLiteral("[-] Grouped outputs")),
          "fanout group selection survives its own rebuild");
    verifyPose(view, beforeGroup, "group expansion with search preserves viewport");
    search->clear();
    auto* output = labelledItem(view, QStringLiteral("output_0"));
    check(output != nullptr, "expanded output exists");
    if (output)
        output->setSelected(true);
    QTest::mouseClick(outputs, Qt::LeftButton);
    QTest::mouseClick(outputs, Qt::LeftButton);
    check(!selected(view, QStringLiteral("output_0")), "removed selection does not resurrect when filter is cleared");

    pose(view);
    view->scene()->clearSelection();
    labelledItem(view, QStringLiteral("tracked"))->setSelected(true);
    const auto beforeReport = capture(view);
    auto renumbered = report;
    renumbered.kernel.id += 100;
    for (auto& n : renumbered.inputs) n.id += 100;
    for (auto& n : renumbered.outputs) n.id += 100;
    for (auto& e : renumbered.edges) { e.fromNodeId += 100; e.toNodeId += 100; }
    for (auto& g : renumbered.outputFanoutGroups)
        for (auto& id : g.nodeIds) id += 100;
    panel.renderReportForTest(renumbered);
    verifyPose(view, beforeReport, "same-target report preserves viewport");
    check(selected(view, QStringLiteral("tracked")), "selection survives node ID changes");

    auto* input = labelledItem(view, QStringLiteral("input_0"));
    check(input != nullptr, "input for ID reuse exists");
    if (input) input->setSelected(true);
    auto replaced = renumbered;
    replaced.inputs[0].displayName = QStringLiteral("replacement");
    replaced.inputs[0].stableKey.symbolName = QStringLiteral("replacement");
    panel.renderReportForTest(replaced);
    check(!selected(view, QStringLiteral("replacement"))
              && selected(view, QStringLiteral("tracked")),
          "reused numeric IDs do not select new symbols; surviving multi-selection remains");

    auto evidenceA = replaced.inputs[1];
    evidenceA.preciseEvidence = true;
    evidenceA.evidenceRange.fileName = QStringLiteral("continuity_fixture.sv");
    evidenceA.evidenceRange.line = 100;
    evidenceA.evidenceRange.column = 3;
    evidenceA.evidenceRange.endLine = 100;
    evidenceA.evidenceRange.endColumn = 9;
    evidenceA.typeDisplayName = QStringLiteral("evidence A");
    auto evidenceB = evidenceA;
    evidenceB.id += 1000;
    evidenceB.evidenceRange.line = evidenceB.evidenceRange.endLine = 101;
    evidenceB.typeDisplayName = QStringLiteral("evidence B");
    replaced.inputs[1] = evidenceA;
    replaced.inputs.append(evidenceB);
    panel.renderReportForTest(replaced);
    auto* evidenceItem = labelledItem(view, QStringLiteral("evidence A"));
    check(evidenceItem != nullptr, "evidence-specific node exists");
    if (evidenceItem) evidenceItem->setSelected(true);
    std::swap(replaced.inputs[1].id, replaced.inputs.last().id);
    panel.renderReportForTest(replaced);
    check(selected(view, QStringLiteral("evidence A"))
              && !selected(view, QStringLiteral("evidence B")),
          "relationship evidence disambiguates nodes sharing a stable symbol");
    replaced.inputs.last().evidenceRange = evidenceA.evidenceRange;
    panel.renderReportForTest(replaced);
    check(!selected(view, QStringLiteral("evidence A"))
              && !selected(view, QStringLiteral("evidence B")),
          "ambiguous identities are cleared instead of selecting two nodes");

    const auto beforeUnavailable = capture(view);
    panel.renderReportForTest({});
    panel.refreshThemePresentation();
    panel.focusFit();
    panel.renderReportForTest(renumbered);
    verifyPose(view, beforeUnavailable, "temporary unavailable report preserves the last valid viewport");
    const auto beforeHide = capture(view);
    host.hide();
    QApplication::processEvents();
    host.show();
    QApplication::processEvents();
    verifyPose(view, beforeHide, "hide and show keep the existing viewport");

    pose(view, 2.4, QPointF(0, 0));
    const auto beforeResize = capture(view);
    qreal resizeError = 0;
    for (int i = 0; i < 10; ++i) {
        host.resize(i % 2 ? QSize(1060, 760) : QSize(920, 650));
        QApplication::processEvents();
        resizeError = qMax(resizeError, centerError(view, beforeResize));
    }
    check(view->transform() == beforeResize.transform && resizeError <= 2.0,
          "repeated viewport resizing preserves zoom and center without drift");

    auto otherTarget = report;
    otherTarget.kernel.displayName = QStringLiteral("another_target");
    otherTarget.kernel.stableKey.symbolName = otherTarget.kernel.displayName;
    pose(view, 2.4);
    panel.renderReportForTest(otherTarget);
    QApplication::processEvents();
    check(view->transform().m11() != 2.4, "new target gets its own initial fit");
    check(view->scene()->selectedItems().isEmpty(), "new target does not inherit selection");
    QTemporaryDir exports;
    check(panel.exportGraph(exports.filePath(QStringLiteral("kernel.svg"))).success,
          "export remains available after target changes");

    RtlInsightWorkbench workbench(nullptr, true);
    workbench.resize(1000, 700);
    auto* hosted = workbench.kernelSurfaceForTest();
    hosted->renderReportForTest(report);
    workbench.show();
    hosted->renderReportForTest(otherTarget);
    hosted->focusZoomIn();
    const auto beforeQueuedFit = capture(hosted->view());
    QApplication::processEvents();
    verifyPose(hosted->view(), beforeQueuedFit,
               "user zoom cancels queued initial fits across rapid target changes");
    workbench.hide();
    workbench.show();
    QApplication::processEvents();
    verifyPose(hosted->view(), beforeQueuedFit,
               "workbench redisplay does not override the kernel viewport");
    workbench.hide();
    hosted->renderReportForTest(report);
    hosted->renderReportForTest({});
    workbench.show();
    QApplication::processEvents();
    hosted->renderReportForTest(report);
    QApplication::processEvents();
    const auto initialFit = capture(hosted->view());
    hosted->focusFit();
    check(hosted->view()->transform() == initialFit.transform,
          "first valid report after an empty target performs the initial fit");

    hosted->renderReportForTest(otherTarget);
    hosted->view()->setTransform(QTransform::fromScale(1.8, 1.8));
    hosted->view()->verticalScrollBar()->triggerAction(QAbstractSlider::SliderSingleStepAdd);
    const auto beforeScrollFit = capture(hosted->view());
    QApplication::processEvents();
    verifyPose(hosted->view(), beforeScrollFit,
               "scrollbar interaction cancels pending initial fit");
    ApplicationThemeManager::instance().setMode(ThemeMode::Dark);
    hosted->focusZoomIn();
    const auto afterUserZoom = capture(hosted->view());
    hosted->refreshThemePresentation();
    verifyPose(hosted->view(), afterUserZoom,
               "theme snapshot cannot overwrite later explicit user zoom");
    ApplicationThemeManager::instance().setMode(ThemeMode::Light);
    hosted->refreshThemePresentation();

    auto* temporaryHost = new QWidget;
    auto* temporaryPanel = new SignalKernelGraphPanelCoordinator(temporaryHost);
    temporaryPanel->dock()->show();
    temporaryHost->show();
    const QPointer<QGraphicsView> destroyedView = temporaryPanel->view();
    temporaryPanel->renderReportForTest(report);
    delete temporaryHost;
    delete temporaryPanel;
    QApplication::processEvents();
    check(destroyedView.isNull(),
          "destroying the parent before the controller cancels pending callbacks safely");

    const QString reviewDir = qEnvironmentVariable("ZEROSLACK_KERNEL_REVIEW_DIR");
    if (!reviewDir.isEmpty()) {
        QDir().mkpath(reviewDir);
        panel.focusFit();
        host.grab().save(QDir(reviewDir).filePath(
            QStringLiteral("kernel-%1.png").arg(view->devicePixelRatioF())));
    }

    std::cout << checks - failures << '/' << checks << " kernel continuity checks passed\n";
    return failures ? 1 : 0;
}
