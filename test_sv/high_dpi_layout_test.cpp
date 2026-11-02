#include "testuistyle.h"
#include "applicationthememanager.h"
#include "commandlayercoordinator.h"
#include "contextdockhost.h"
#include "liveinsightscontextprovider.h"
#include "liveinsightscontextview.h"
#include "liveinsighttoolpage.h"
#include "live_insight_test_utils.h"
#include "rtlinsightspanelcoordinator.h"
#include "rtlinsightworkbench.h"
#include "settingscenterpanel.h"
#include "settingscenterservice.h"
#include "signal_trace_test_fixture.h"
#include "navigationwidget.h"
#include "hierarchyservice.h"
#include "semanticindex.h"
#include "slangmanager.h"
#include "workspaceconfigurationdialog.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStyleOptionSpinBox>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <cstdio>

namespace {
QJsonArray records;
QSet<QString> observedHelpers;
QSet<QString> encounteredHelpers;
QString artifactDir;
int failures = 0;

QString id(QWidget* widget)
{
    QString result = QString::fromLatin1(widget->metaObject()->className())
        + QLatin1Char('#') + widget->objectName();
    if (widget->objectName().isEmpty()) {
        if (auto* button = qobject_cast<QAbstractButton*>(widget)) result += button->text();
        if (widget->parentWidget()) result += QLatin1Char('@') + widget->parentWidget()->objectName();
    }
    return result;
}
QJsonArray sizeJson(QSize size) { return {size.width(), size.height()}; }
QJsonArray rectJson(QRect rect) { return {rect.x(), rect.y(), rect.width(), rect.height()}; }
void settle()
{
    QApplication::processEvents();
    QTest::qWait(40);
    QApplication::processEvents();
}
bool isControl(QWidget* widget)
{
    return qobject_cast<QAbstractButton*>(widget) || qobject_cast<QLineEdit*>(widget)
        || qobject_cast<QComboBox*>(widget) || qobject_cast<QAbstractSpinBox*>(widget)
        || qobject_cast<QSlider*>(widget)
        || (qobject_cast<QAbstractItemView*>(widget) && !qobject_cast<QHeaderView*>(widget));
}
QList<QScrollArea*> scrollAncestors(QWidget* widget, QWidget* window)
{
    QList<QScrollArea*> result;
    for (auto* parent = widget->parentWidget(); parent && parent != window; parent = parent->parentWidget()) {
        auto* scroll = qobject_cast<QScrollArea*>(parent);
        if (scroll && scroll->widget() && scroll->widget()->isAncestorOf(widget)) result.append(scroll);
    }
    return result;
}
QRect rectIn(QWidget* widget, QWidget* window)
{
    return QRect(widget->mapTo(window, QPoint()), widget->size());
}
QRect displayedRect(QWidget* widget, QWidget* window)
{
    QRect result = rectIn(widget, window);
    for (auto* scroll : scrollAncestors(widget, window))
        result = result.intersected(rectIn(scroll->viewport(), window));
    return result;
}
QRect spinEditField(QAbstractSpinBox* spin)
{
    QStyleOptionSpinBox option;
    option.initFrom(spin);
    option.frame = spin->hasFrame();
    option.buttonSymbols = spin->buttonSymbols();
    return spin->style()->subControlRect(QStyle::CC_SpinBox, &option,
                                        QStyle::SC_SpinBoxEditField, spin);
}

bool belowIntrinsicMinimum(QWidget* widget)
{
    if (auto* edit = qobject_cast<QLineEdit*>(widget)) {
        if (auto* spin = qobject_cast<QAbstractSpinBox*>(widget->parentWidget())) {
            // This frameless editor occupies the parent's native subcontrol,
            // not a separately framed line edit's minimumSizeHint.
            const QRect field = spinEditField(spin);
            const QRect content = edit->contentsRect().marginsRemoved(edit->textMargins());
            return edit->geometry() != field
                || content.height() < edit->fontMetrics().tightBoundingRect(edit->displayText()).height()
                || content.width() < edit->fontMetrics().horizontalAdvance(edit->displayText());
        }
    }
    const QSize minimum = widget->minimumSizeHint();
    return widget->width() < minimum.width() || widget->height() < minimum.height();
}

void verifyIntrinsicGeometryRejection()
{
    QPushButton probe(QStringLiteral("Geometry probe"));
    probe.ensurePolished();
    const QSize minimum = probe.minimumSizeHint();
    if (minimum.width() <= 1 || minimum.height() <= 1) { ++failures; return; }
    probe.resize(minimum);
    const bool validAccepted = !belowIntrinsicMinimum(&probe);
    probe.resize(minimum.width() - 1, minimum.height());
    const bool shortWidthRejected = belowIntrinsicMinimum(&probe);
    probe.resize(minimum.width(), minimum.height() - 1);
    const bool shortHeightRejected = belowIntrinsicMinimum(&probe);
    failures += !validAccepted + !shortWidthRejected + !shortHeightRejected;
    records.append(QJsonObject{{"case", "intrinsic-geometry-negative-controls"},
        {"validAccepted", validAccepted}, {"onePixelShortWidthRejected", shortWidthRejected},
        {"onePixelShortHeightRejected", shortHeightRejected}, {"minimum", sizeJson(minimum)}});

    QSpinBox spin;
    spin.setPrefix(QStringLiteral("Levels "));
    spin.setRange(1, 8);
    spin.ensurePolished();
    spin.resize(spin.sizeHint());
    spin.show();
    QCoreApplication::processEvents();
    auto* edit = spin.findChild<QLineEdit*>();
    if (!edit) { ++failures; return; }
    const QRect nativeField = spinEditField(&spin);
    const bool nativeAccepted = !belowIntrinsicMinimum(edit);
    edit->setGeometry(nativeField.adjusted(0, 0, -1, 0));
    const bool nativeShortWidthRejected = belowIntrinsicMinimum(edit);
    edit->setGeometry(nativeField.adjusted(0, 0, 0, -1));
    const bool nativeShortHeightRejected = belowIntrinsicMinimum(edit);
    failures += !nativeAccepted + !nativeShortWidthRejected + !nativeShortHeightRejected;
    records.append(QJsonObject{{"case", "native-spin-edit-negative-controls"},
        {"nativeField", rectJson(nativeField)}, {"validAccepted", nativeAccepted},
        {"onePixelShortWidthRejected", nativeShortWidthRejected},
        {"onePixelShortHeightRejected", nativeShortHeightRejected}});
}

QJsonObject audit(QWidget* window, const QString& name)
{
    QJsonArray controls, intersections, reachability;
    QList<QWidget*> visible;
    int outside = 0, undersized = 0;
    for (auto* widget : window->findChildren<QWidget*>()) {
        if (!widget->isVisibleTo(window) || !isControl(widget)) continue;
        const QRect rect = rectIn(widget, window);
        const QRect displayed = displayedRect(widget, window);
        const QSize minimum = widget->minimumSizeHint();
        QWidget* titleBar = nullptr;
        for (auto* ancestor = widget->parentWidget(); ancestor && ancestor != window; ancestor = ancestor->parentWidget())
            if (ancestor->inherits("ElaAppBar")) titleBar = ancestor;
        // Ela reserves contentsMargins for its title bar. Check its controls against
        // that bar and the window, and keep all form controls inside contentsRect.
        const QRect bounds = titleBar ? rectIn(titleBar, window).intersected(window->rect()) : window->contentsRect();
        const bool inside = displayed.isEmpty() || (bounds.contains(displayed)
            && (!titleBar || !window->contentsRect().intersects(displayed)));
        const bool below = belowIntrinsicMinimum(widget);
        outside += !inside;
        undersized += below;
        const QString helper = widget->property("_zeroslackInsightThemeHelper").toString();
        if (!helper.isEmpty()) observedHelpers.insert(helper + QLatin1Char(':') + id(widget));
        QString text;
        if (auto* button = qobject_cast<QAbstractButton*>(widget)) text = button->text();
        if (auto* edit = qobject_cast<QLineEdit*>(widget)) text = edit->text();
        if (auto* combo = qobject_cast<QComboBox*>(widget)) text = combo->currentText();
        QJsonObject control{{"widget", id(widget)}, {"parent", id(widget->parentWidget())},
            {"rect", rectJson(rect)}, {"minimumSizeHint", sizeJson(minimum)}, {"text", text},
            {"inside", inside}, {"belowMinimumHint", below}, {"helper", helper},
            {"surface", titleBar ? "titlebar" : "content"}, {"bounds", rectJson(bounds)},
            {"displayedRect", rectJson(displayed)}, {"scrollClipped", rect != displayed},
            {"font", widget->font().toString()}};
        if (auto* edit = qobject_cast<QLineEdit*>(widget))
            if (auto* spin = qobject_cast<QAbstractSpinBox*>(widget->parentWidget())) {
                control.insert("intrinsicGeometryContract", "native-spin-edit-field");
                control.insert("nativeFieldInParent", rectJson(spinEditField(spin)));
                control.insert("textInkSize", sizeJson(edit->fontMetrics().tightBoundingRect(edit->displayText()).size()));
            }
        controls.append(control);
        visible.append(widget);
    }
    for (int i = 0; i < visible.size(); ++i) {
        for (int j = i + 1; j < visible.size(); ++j) {
            auto* a = visible[i];
            auto* b = visible[j];
            if (a->isAncestorOf(b) || b->isAncestorOf(a)) continue;
            // Controls in the same scroll content must not overlap even offscreen.
            // Between distinct scroll contents, compare their actually displayed rectangles.
            const bool sameViewport = scrollAncestors(a, window) == scrollAncestors(b, window);
            const QRect overlap = (sameViewport ? rectIn(a, window) : displayedRect(a, window)).intersected(
                sameViewport ? rectIn(b, window) : displayedRect(b, window));
            if (!overlap.isEmpty()) intersections.append(QJsonObject{{"a", id(a)}, {"b", id(b)}, {"rect", rectJson(overlap)}});
        }
    }
    failures += outside + undersized + intersections.size();
    QJsonObject result{{"case", name}, {"size", sizeJson(window->size())},
        {"minimumSize", sizeJson(window->minimumSize())}, {"minimumSizeHint", sizeJson(window->minimumSizeHint())},
        {"controls", controls}, {"intersections", intersections},
        {"outside", outside}, {"undersized", undersized}};
    if (auto* categories = window->findChild<QListWidget*>("settingsCenterCategoryList")) {
        const bool labelsFit = categories->viewport()->width() >= categories->sizeHintForColumn(0);
        result["categoryLabelsFit"] = labelsFit;
        failures += !labelsFit;
        if (!labelsFit) fprintf(stderr, "%s: category text would be elided\n", qPrintable(name));
    }
    if (!artifactDir.isEmpty()) {
        const QString filename = name + QStringLiteral(".png");
        if (!window->grab().save(artifactDir + QLatin1Char('/') + filename)) ++failures;
        result["screenshot"] = filename;
    }
    QHash<QScrollArea*, QPoint> scrollPositions;
    for (auto* scroll : window->findChildren<QScrollArea*>())
        scrollPositions.insert(scroll, QPoint(scroll->horizontalScrollBar()->value(), scroll->verticalScrollBar()->value()));
    int unreachable = 0;
    for (auto* widget : visible) {
        const auto ancestors = scrollAncestors(widget, window);
        if (ancestors.isEmpty()) continue;
        for (auto* scroll : ancestors) {
            // ensureWidgetVisible may only expose an editor's input-method cursor rectangle.
            // Move the scrollbars far enough to expose the entire control instead.
            const QRect target = rectIn(widget, window);
            const QRect viewport = rectIn(scroll->viewport(), window);
            const int dx = target.left() < viewport.left() ? target.left() - viewport.left()
                : qMax(0, target.right() - viewport.right());
            const int dy = target.top() < viewport.top() ? target.top() - viewport.top()
                : qMax(0, target.bottom() - viewport.bottom());
            scroll->horizontalScrollBar()->setValue(scroll->horizontalScrollBar()->value() + dx);
            scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->value() + dy);
            QApplication::processEvents();
        }
        const QRect rect = rectIn(widget, window);
        bool fullyVisible = window->contentsRect().contains(rect);
        QJsonArray viewports;
        for (auto* scroll : ancestors) {
            const QRect viewport = rectIn(scroll->viewport(), window);
            fullyVisible = fullyVisible && viewport.contains(rect);
            viewports.append(QJsonObject{{"scroll", id(scroll)}, {"rect", rectJson(viewport)}});
        }
        // An item view can be taller than its outer viewport on a small
        // screen. Its two ends must both be reachable by scrolling; buttons
        // and inputs still have to fit and be wholly visible at once.
        bool topReachable = fullyVisible, bottomReachable = fullyVisible;
        if (!fullyVisible && qobject_cast<QAbstractItemView*>(widget)) {
            const auto revealEnd = [&](bool bottom) {
                for (auto* scroll : ancestors) {
                    const QRect target = rectIn(widget, window);
                    const QRect viewport = rectIn(scroll->viewport(), window);
                    const int dy = bottom ? target.bottom() - viewport.bottom()
                                          : target.top() - viewport.top();
                    scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->value() + dy);
                    QApplication::processEvents();
                }
                const QRect target = rectIn(widget, window);
                const int y = bottom ? target.bottom() : target.top();
                const QRect edge(target.left(), y, target.width(), 1);
                bool reached = window->contentsRect().contains(edge);
                for (auto* scroll : ancestors)
                    reached = reached && rectIn(scroll->viewport(), window).contains(edge);
                return reached;
            };
            topReachable = revealEnd(false);
            bottomReachable = revealEnd(true);
        }
        const bool reachable = fullyVisible || (topReachable && bottomReachable);
        unreachable += !reachable;
        reachability.append(QJsonObject{{"widget", id(widget)}, {"rect", rectJson(rect)},
            {"viewports", viewports}, {"fullyVisible", fullyVisible},
            {"topReachable", topReachable}, {"bottomReachable", bottomReachable}, {"reachable", reachable}});
    }
    for (auto it = scrollPositions.cbegin(); it != scrollPositions.cend(); ++it) {
        it.key()->horizontalScrollBar()->setValue(it.value().x());
        it.key()->verticalScrollBar()->setValue(it.value().y());
    }
    failures += unreachable;
    result["scrollReachability"] = reachability;
    result["unreachable"] = unreachable;
    QJsonArray hiddenHelpers;
    for (auto* widget : window->findChildren<QWidget*>()) {
        const QString helper = widget->property("_zeroslackInsightThemeHelper").toString();
        if (helper.isEmpty() || !isControl(widget)) continue;
        encounteredHelpers.insert(helper + QLatin1Char(':') + id(widget));
        if (widget->isVisibleTo(window)) continue;
        QJsonArray parents;
        for (auto* parent = widget; parent && parent != window; parent = parent->parentWidget())
            parents.append(QJsonObject{{"widget", id(parent)}, {"explicitlyHidden", parent->isHidden()}});
        hiddenHelpers.append(QJsonObject{{"widget", id(widget)}, {"parents", parents}});
    }
    result["hiddenHelpers"] = hiddenHelpers;
    qInfo().noquote() << name << "overlap/outside/undersized" << intersections.size() << outside << undersized;
    if (outside || undersized || !intersections.isEmpty() || unreachable)
        fprintf(stderr, "%s: overlap=%lld outside=%d undersized=%d unreachable=%d\n",
            qPrintable(name), static_cast<long long>(intersections.size()), outside, undersized, unreachable);
    return result;
}

bool hasEmbeddedControls(QMenu* menu)
{
    for (auto* action : menu->actions()) {
        if (!action->isVisible()) continue;
        if (auto* widgetAction = qobject_cast<QWidgetAction*>(action))
            if (widgetAction->defaultWidget()) return true;
        if (action->menu() && hasEmbeddedControls(action->menu())) return true;
    }
    return false;
}

void captureMenu(QMenu* menu, const QPoint& position, const QString& name)
{
    menu->popup(position);
    settle();
    failures += !menu->isVisible() || !menu->screen()->availableGeometry().contains(menu->geometry());
    records.append(audit(menu, name));
    int index = 0;
    for (auto* action : menu->actions()) {
        auto* child = action->menu();
        if (!action->isVisible() || !child || !hasEmbeddedControls(child)) continue;
        menu->setActiveAction(action);
        captureMenu(child, menu->mapToGlobal(menu->actionGeometry(action).topRight()),
                    name + QStringLiteral("-submenu-%1").arg(index++));
    }
    menu->close();
    settle();
}

void capture(QWidget* content, const QString& name, std::function<void()> ready = {})
{
    QPointer<QWidget> originalParent = content->parentWidget();
    auto* originalDock = qobject_cast<QDockWidget*>(originalParent.data());
    QWidget host;
    host.setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    auto* layout = new QVBoxLayout(&host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSizeConstraint(QLayout::SetNoConstraint);
    layout->addWidget(content);
    host.setFixedSize(QApplication::primaryScreen()->availableGeometry().size() * .95);
    host.show(); content->show(); settle();
    if (ready) { ready(); settle(); }
    QJsonObject record = audit(&host, name);
    record["contentMinimumSize"] = sizeJson(content->minimumSize());
    record["contentMinimumSizeHint"] = sizeJson(content->minimumSizeHint());
    records.append(record);
    for (auto* button : content->findChildren<QToolButton*>()) {
        auto* menu = button->menu();
        if (!menu || !button->isVisibleTo(&host)) continue;
        if (!hasEmbeddedControls(menu)) continue;
        captureMenu(menu, button->mapToGlobal(QPoint(0, button->height())),
                    name + QStringLiteral("-menu-") + button->objectName());
    }
    content->hide(); layout->removeWidget(content);
    if (originalDock) originalDock->setWidget(content);
    else content->setParent(originalParent);
}

const QString source = QStringLiteral(R"SV(`timescale 1ns/1ps
module dpi_leaf(input logic clk, input logic ready, output logic valid);
  always_ff @(posedge clk) valid <= ready;
endmodule
module dpi_top(input logic clk, input logic rst, input logic start, output logic done);
  typedef enum logic [1:0] { IDLE, BUSY, FINISH } state_t;
  state_t state, next_state;
  logic ready;
  always_ff @(posedge clk) begin
    if (rst) state <= IDLE;
    else state <= next_state;
  end
  always_comb begin
    next_state = state;
    ready = 1'b0;
    case (state)
      IDLE: if (start) next_state = BUSY;
      BUSY: begin
        ready = 1'b1;
        next_state = FINISH;
      end
      FINISH: next_state = IDLE;
      default: next_state = IDLE;
    endcase
  end
  dpi_leaf worker(.clk(clk), .ready(ready), .valid(done));
endmodule
)SV");

void runCases(const QString& fixture, const QString& settingsDir)
{
    QFile file(fixture);
    if (!file.open(QIODevice::WriteOnly)) qFatal("Cannot write test fixture");
    file.write(source.toUtf8()); file.close();
    SignalTraceTestFixture nativeFixture;
    if(!nativeFixture.build(source+"\nmodule top;dpi_top u();endmodule\n"))qFatal("Production semantic fixture failed");
    SemanticIndex::getInstance()->setSnapshot(nativeFixture.context.token.snapshot);
    const QString analyzedFile=nativeFixture.file;
    QWidget owner;
    SignalTracePanel trace(&owner);trace.setContext(nativeFixture.context);trace.showForSymbol("ready",analyzedFile,"dpi_top");
    capture(&trace,"signal-trace",[&]{if(!QTest::qWaitFor([&]{return !trace.busy();},10000))++failures;});
    trace.setHeatmapMode(true);capture(&trace,"signal-heatmap");
    NavigationWidget design;
    const QList<DesignHierarchyRoot> roots{{"main","Main","top",analyzedFile,true}};
    design.updateDesignHierarchy(HierarchyService(nativeFixture.context.token).explicitDesignHierarchy(roots,"main",{analyzedFile}));
    design.setActiveTab(NavigationWidget::DesignTab);capture(&design,"design-hierarchy");
    RtlInsightsPanelCoordinator rtl(&owner);
    rtl.showStateTransitionGraphForSignal(analyzedFile, "dpi_top", "next_state");
    if (!waitForLiveInsightReports(rtl)) ++failures;
    capture(rtl.dock()->widget(), "rtl-state", [&] { rtl.focusFit(); });
    RtlInsightWorkbench generic;
    capture(&generic, "workbench");
    failures += !generic.setViewKind(InsightWorkbenchViewKind::StateTransition);
    capture(&generic, "workbench-state-empty");

    CommandLayerPickerPanel picker;
    // Exercise its real popup and its real screen-clamping code, not a substitute line edit.
    QWidget anchor;
    anchor.setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    anchor.resize(QApplication::primaryScreen()->availableGeometry().size() * .95);
    anchor.show(); settle();
    picker.showFor(&anchor); settle();
    auto pickerRecord = audit(&picker, "command-picker");
    const bool pickerOnScreen = QApplication::primaryScreen()->availableGeometry().contains(picker.frameGeometry());
    pickerRecord["frameInsideAvailableGeometry"] = pickerOnScreen;
    failures += !pickerOnScreen;
    records.append(pickerRecord);
    picker.hide(); anchor.hide();

    LiveInsightsContextView overview(nullptr);
    capture(&overview, "context-overview");
    LiveInsightToolContext context;
    context.workspaceId = "dpi-workspace"; context.documentId = "dpi-document";
    context.fileName = analyzedFile; context.projectId=nativeFixture.context.projectId;context.workspaceId=nativeFixture.context.workspaceId;context.documentText=nativeFixture.source; context.moduleName = "dpi_top"; context.signalName = "ready";
    for (auto kind : {LiveInsightKind::Kernel, LiveInsightKind::Module, LiveInsightKind::State, LiveInsightKind::Hotspot}) {
        ContextDockHost sidebar;
        auto resource = LiveInsightsContextProvider::resourceForKind(kind, {});
        auto* view = new LiveInsightsContextView(nullptr, kind);
        auto current = context;
        if (kind == LiveInsightKind::State) current.signalName = "next_state";
        view->setToolContextSource([&] { return current; });
        view->setTargetPickRequest([](LiveInsightKind, LiveInsightsContextView*, LiveInsightSession*, std::function<void(const LiveInsightsContextView::TargetCandidate&)>) { return false; });
        if (!sidebar.addResource(resource, view, true)) qFatal("Cannot add actual sidebar section");
        // Reproduce construction on a non-current stack page before the real sidebar width is known.
        QStackedWidget pages;
        pages.addWidget(new QWidget);
        pages.addWidget(&sidebar);
        pages.resize(QApplication::primaryScreen()->availableGeometry().size() * .95);
        pages.show(); settle();
        pages.setCurrentWidget(&sidebar); settle();
        pages.setCurrentIndex(0); settle();
        pages.removeWidget(&sidebar);
        sidebar.setParent(nullptr);
        pages.hide();
        capture(&sidebar, "sidebar-" + liveInsightKindId(kind), [&] {
            sidebar.setSectionHeight(resource.stableKey(), sidebar.height());
            if (kind == LiveInsightKind::State) {
                auto* page = view->surfaceForTest();
                auto* panel = page ? page->workbenchForTest()->rtlSurfaceForTest() : nullptr;
                if (!panel || !waitForLiveInsightReports(*panel)) ++failures;
            }
        });
        if (!view->surfaceForTest()) { ++failures; continue; }
        auto* page=view->surfaceForTest();auto* workbench=page->workbenchForTest();
        if(page->minimumSizeHint().height()>QApplication::primaryScreen()->availableGeometry().height())++failures;
        QJsonObject dimensions{{"case","sidebar-minimum-"+liveInsightKindId(kind)},
            {"pageMinimum",sizeJson(page->minimumSize())},{"pageMinimumHint",sizeJson(page->minimumSizeHint())}};
        if(workbench && workbench->rtlSurfaceForTest())dimensions["dockMinimum"]=sizeJson(workbench->rtlSurfaceForTest()->dock()->minimumSize());
        records.append(dimensions);
        if (kind == LiveInsightKind::Kernel) {
            current.moduleName.clear(); current.signalName.clear();
            view->setToolContextSource([&] { return current; });
            capture(&sidebar, "context-candidates");
        }
    }

    SettingsCenterService settings(settingsDir + "/global.ini", settingsDir + "/workspace.json");
    SettingsCenterPanel panel(&settings, QFileInfo(fixture).absolutePath());
    for (const auto& category : SettingsCenterSchema::categories()) {
        panel.selectCategory(category.id);
        capture(&panel, "settings-" + category.id);
    }
    WorkspaceConfigurationDialog configuration;
    configuration.show(); settle();
    auto configurationRecord = audit(&configuration, "workspace-configuration");
    const bool configurationOnScreen = QApplication::primaryScreen()->availableGeometry().contains(configuration.frameGeometry());
    configurationRecord["frameInsideAvailableGeometry"] = configurationOnScreen;
    failures += !configurationOnScreen;
    records.append(configurationRecord);
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    if (!initializeUiStyleForTest()) return 3;
    ApplicationThemeManager::instance().applyToApplication();
    const QString scale = qEnvironmentVariable("QT_SCALE_FACTOR");
    const QString root = qEnvironmentVariable("ZEROSLACK_TEST_ARTIFACT_DIR");
    if (!root.isEmpty()) { artifactDir = root + QLatin1Char('/') + scale; QDir().mkpath(artifactDir); }
    const QString fixtureDir = qEnvironmentVariable("ZEROSLACK_HIDPI_FIXTURE_DIR", temporary.path());
    QDir().mkpath(fixtureDir);
    verifyIntrinsicGeometryRejection();
    runCases(fixtureDir + "/dpi.sv", temporary.path());
    const QSet<QString> missingHelpers = encounteredHelpers - observedHelpers;
    failures += missingHelpers.size();
    if (!missingHelpers.isEmpty())
        fprintf(stderr, "Helpers without visible geometry coverage: %s\n", qPrintable(missingHelpers.values().join(", ")));
    const QJsonObject output{{"scale", scale}, {"availableGeometry", rectJson(app.primaryScreen()->availableGeometry())},
        {"records", records}, {"observedHelpers", QJsonArray::fromStringList(observedHelpers.values())},
        {"missingHelpers", QJsonArray::fromStringList(missingHelpers.values())}, {"failures", failures}};
    if (!artifactDir.isEmpty()) {
        QFile file(artifactDir + "/measurements.json");
        if (!file.open(QIODevice::WriteOnly)) return 2;
        file.write(QJsonDocument(output).toJson());
    }
    return failures == 0 ? 0 : 1;
}
