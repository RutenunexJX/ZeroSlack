#include "applicationthememanager.h"
#include "commandlayercoordinator.h"
#include "contextdockhost.h"
#include "liveinsightscontextprovider.h"
#include "liveinsightscontextview.h"
#include "liveinsighttoolpage.h"
#include "rtlinsightspanelcoordinator.h"
#include "rtlinsightworkbench.h"
#include "settingscenterpanel.h"
#include "settingscenterservice.h"
#include "signalkernelgraphpanelcoordinator.h"
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
#include <QPointer>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSlider>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QVBoxLayout>

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
        const bool inside = displayed.isEmpty() || window->contentsRect().contains(displayed);
        const bool below = widget->width() < minimum.width() || widget->height() < minimum.height();
        outside += !inside;
        undersized += below;
        const QString helper = widget->property("_zeroslackInsightThemeHelper").toString();
        if (!helper.isEmpty()) observedHelpers.insert(helper + QLatin1Char(':') + id(widget));
        QString text;
        if (auto* button = qobject_cast<QAbstractButton*>(widget)) text = button->text();
        if (auto* edit = qobject_cast<QLineEdit*>(widget)) text = edit->text();
        if (auto* combo = qobject_cast<QComboBox*>(widget)) text = combo->currentText();
        controls.append(QJsonObject{{"widget", id(widget)}, {"parent", id(widget->parentWidget())},
            {"rect", rectJson(rect)}, {"minimumSizeHint", sizeJson(minimum)}, {"text", text},
            {"inside", inside}, {"belowMinimumHint", below}, {"helper", helper},
            {"displayedRect", rectJson(displayed)}, {"scrollClipped", rect != displayed},
            {"font", widget->font().toString()}});
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
        unreachable += !fullyVisible;
        reachability.append(QJsonObject{{"widget", id(widget)}, {"rect", rectJson(rect)},
            {"viewports", viewports}, {"fullyVisible", fullyVisible}});
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
    SlangManager slang;
    SemanticIndex::getInstance()->updateSymbolRecordsForFile(fixture, slang.extractSymbolRecords(fixture, source), source);
    QWidget owner;
    SignalKernelGraphPanelCoordinator kernel(&owner);
    kernel.showSignalKernelGraphForSymbol("ready", fixture, "dpi_top");
    capture(kernel.dock()->widget(), "kernel", [&] { kernel.focusFit(); });

    RtlInsightsPanelCoordinator rtl(&owner);
    rtl.showModuleBlockDiagramForModule(fixture, "dpi_top");
    capture(rtl.dock()->widget(), "rtl-module", [&] { rtl.focusFit(); });
    rtl.showStateTransitionGraphForSignal(fixture, "dpi_top", "next_state");
    capture(rtl.dock()->widget(), "rtl-state", [&] { rtl.focusFit(); });
    RtlInsightWorkbench generic;
    capture(&generic, "workbench");

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
    context.fileName = fixture; context.moduleName = "dpi_top"; context.signalName = "ready";
    for (auto kind : {LiveInsightKind::Kernel, LiveInsightKind::Module, LiveInsightKind::State, LiveInsightKind::Hotspot}) {
        ContextDockHost sidebar;
        auto resource = LiveInsightsContextProvider::resourceForKind(kind, {});
        auto* view = new LiveInsightsContextView(nullptr, kind);
        auto current = context;
        if (kind == LiveInsightKind::State) current.signalName = "next_state";
        view->setToolContextSource([&] { return current; });
        view->setTargetPickRequest([](LiveInsightKind, std::function<void(const LiveInsightsContextView::TargetCandidate&)>) { return false; });
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
        });
        if (!view->surfaceForTest()) { ++failures; continue; }
        auto* workbench = view->surfaceForTest()->workbenchForTest();
        if (workbench->minimumSizeHint().height() > QApplication::primaryScreen()->availableGeometry().height()) ++failures;
        QJsonObject dimensions{{"case", "sidebar-minimum-" + liveInsightKindId(kind)},
            {"workbenchMinimum", sizeJson(workbench->minimumSize())},
            {"workbenchMinimumHint", sizeJson(workbench->minimumSizeHint())}};
        if (auto* surface = workbench->kernelSurfaceForTest()) dimensions["dockMinimum"] = sizeJson(surface->dock()->minimumSize());
        if (auto* surface = workbench->rtlSurfaceForTest()) {
            dimensions["dockMinimum"] = sizeJson(surface->dock()->minimumSize());
            dimensions["stackMinimum"] = sizeJson(surface->stackForTest()->minimumSize());
        }
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
    if (qEnvironmentVariable("ZEROSLACK_TEST_UI_STYLE") == "ela"
        && !ApplicationThemeManager::instance().selectBackend(UiStyleBackend::Ela)) return 3;
    ApplicationThemeManager::instance().applyToApplication();
    const QString scale = qEnvironmentVariable("QT_SCALE_FACTOR");
    const QString root = qEnvironmentVariable("ZEROSLACK_TEST_ARTIFACT_DIR");
    if (!root.isEmpty()) { artifactDir = root + QLatin1Char('/') + scale; QDir().mkpath(artifactDir); }
    const QString fixtureDir = qEnvironmentVariable("ZEROSLACK_HIDPI_FIXTURE_DIR", temporary.path());
    QDir().mkpath(fixtureDir);
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
