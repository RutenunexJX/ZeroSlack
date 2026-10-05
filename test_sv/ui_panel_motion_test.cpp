#include "applicationthememanager.h"
#include "contextdockhost.h"
#include "contextdocktransition.h"
#include "contextfloatingwindow.h"
#include "contextworkspacecontroller.h"
#include "navigationpanecoordinator.h"
#include "nativepanelcomposition.h"
#include "uicontrols.h"
#include "panelmotion.h"
#include <QStackedWidget>
#ifdef ZEROSLACK_ENABLE_ELA
#include "Development/ElaCentralStackedWidget.h"
#endif
#include "panellayoutcontroller.h"
#include "testuistyle.h"
#include "mainwindow.h"
#include "mycodeeditor.h"
#include "tabmanager.h"
#include "liveinsightscontextprovider.h"
#ifdef ZEROSLACK_ENABLE_ELA
#include "ElaDrawerArea.h"
#endif
#include <QApplication>
#include <QDockWidget>
#include <QDir>
#include <QLabel>
#include <QLayout>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QAction>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QStyle>
#include <QTimer>

namespace {
class Editor final : public QPlainTextEdit {
public:
    int resizes = 0;
    void resizeEvent(QResizeEvent* event) override { ++resizes; QPlainTextEdit::resizeEvent(event); }
};

struct Harness {
    QMainWindow window;
    Editor* editor = new Editor;
    PanelLayoutController* drawer;
    ContextWorkspaceController* context;
    NavigationPaneCoordinator* navigation;
    QStringList keys;
    Harness() {
        window.setCentralWidget(editor);
        editor->setPlainText("module retained;\n  logic value;\nendmodule\n");
        editor->document()->setModified(false);
        navigation = new NavigationPaneCoordinator(&window);
        window.addDockWidget(Qt::LeftDockWidgetArea, navigation->dock());
        drawer = new PanelLayoutController(&window, &window);
        for (const auto& id : {QString("problems"), QString("activity")}) {
            auto* dock = new QDockWidget(id, &window);
            auto* content = new QPlainTextEdit;
            content->setPlainText(id + " content retained across transitions");
            dock->setWidget(content);
            window.addDockWidget(Qt::BottomDockWidgetArea, dock);
            drawer->registerBottomPanel(id, dock);
        }
        drawer->finalize();
        drawer->setBottomCollapsed(true);
        context = new ContextWorkspaceController(&window, editor, &window);
        for (int i = 0; i < 2; ++i) {
            ContextResource resource;
            resource.providerId = "motion-test";
            resource.resourceId = QString::number(i);
            resource.uri = QUrl("motion:section/" + resource.resourceId);
            resource.title = "Section " + resource.resourceId;
            auto* label = new QLabel("Retained view " + resource.resourceId);
            label->setMinimumHeight(64);
            context->dockHost()->addResource(resource, label);
            keys.append(resource.stableKey());
        }
        window.resize(1200, 780);
        window.show(); window.activateWindow();
        QApplication::processEvents(); QTest::qWait(40);
    }
};

QRect inWindow(QWidget* widget, QWidget* window) {
    return QRect(widget->mapTo(window, QPoint()), widget->size());
}
}

class UiPanelMotionTest final : public QObject {
    Q_OBJECT
private slots:
    void rightSidebarResizesRealEditor_data() {
        QTest::addColumn<bool>("toolbox");
        QTest::newRow("provider") << false;
        QTest::newRow("toolbox") << true;
    }
    void rightSidebarResizesRealEditor() {
#ifdef ZEROSLACK_ENABLE_ELA
        QFETCH(bool, toolbox);
        const bool previousAnimations = ApplicationThemeManager::instance().animationsEnabled();
        const auto restoreAnimations = qScopeGuard([=] { ApplicationThemeManager::instance().setAnimationsEnabled(previousAnimations); });
        ApplicationThemeManager::instance().setAnimationsEnabled(true);
        QTemporaryDir files; QVERIFY(files.isValid());
        const QString path = files.filePath("layout.sv");
        QFile source(path); QVERIFY(source.open(QIODevice::WriteOnly));
        source.write("module layout;\n  logic retained;\nendmodule\n"); source.close();
        MainWindow window; window.resize(1280, 800); window.show();
        QVERIFY(window.tabManager->openFileInTab(path));
        auto* editor = window.tabManager->getCurrentEditor(); QVERIFY(editor);
        auto* context = window.findChild<ContextWorkspaceController*>(); QVERIFY(context);
        auto* motion = window.findChild<ElaDrawerArea*>("contextSidebarDrawer"); QVERIFY(motion);
        auto* action = window.findChild<QAction*>(toolbox ? "contextRail.toolbox" : "contextRail.temporaryEditor"); QVERIFY(action);
        context->setDockVisible(false); motion->finishDrawerAnimation(); QTest::qWait(40);
        const int closedWidth = editor->viewport()->width();
        const QString original = editor->toPlainText();
        const auto sample = [&](const QString& phase) {
            QJsonArray rows;
            QElapsedTimer timer; timer.start();
            do {
                rows.append(QJsonObject{{"ms",timer.nsecsElapsed()/1e6},
                    {"viewportWidth",editor->viewport()->width()},
                    {"editorWidth",editor->width()},
                    {"dockWidth",context->dockWidget()->isVisible() ? context->dockWidget()->width() : 0}});
                if (!motion->isDrawerAnimating()) break;
                QTest::qWait(8);
            } while (timer.elapsed() < 2000);
            const QString evidence = qEnvironmentVariable("ZEROSLACK_UI_EVIDENCE_DIR");
            if (!evidence.isEmpty()) {
                QDir().mkpath(evidence);
                QFile output(evidence + '/' + (toolbox ? "toolbox-" : "provider-") + phase + ".json");
                if (output.open(QIODevice::WriteOnly)) output.write(QJsonDocument(rows).toJson());
            }
            return rows;
        };
        action->trigger();
        QVERIFY(motion->isDrawerAnimating());
        const auto opening = sample("open");
        QVERIFY(!motion->isDrawerAnimating());
        const int openWidth = editor->viewport()->width();
        QVERIFY(openWidth < closedWidth);
        const int separator = window.style()->pixelMetric(QStyle::PM_DockWidgetSeparatorExtent);
        QVERIFY(qAbs(opening.first().toObject().value("viewportWidth").toInt() - closedWidth) <= separator + 2);
        const auto checkContinuous = [&](const QJsonArray& rows, bool opening) {
            for (int i = 1; i < rows.size(); ++i) {
                const int previous = rows.at(i-1).toObject().value("viewportWidth").toInt();
                const int current = rows.at(i).toObject().value("viewportWidth").toInt();
                if (opening ? current > previous : current < previous) return false;
            }
            return rows.size() >= 2 && qAbs(rows.last().toObject().value("viewportWidth").toInt()
                - rows.at(rows.size()-2).toObject().value("viewportWidth").toInt()) <= separator + 2;
        };
        QVERIFY(checkContinuous(opening, true));
        const auto intermediateWidths = [](const QJsonArray& rows, int low, int high) {
            QSet<int> widths;
            for (const auto& row : rows) {
                const int value = row.toObject().value("viewportWidth").toInt();
                if (value > low + 1 && value < high - 1) widths.insert(value);
            }
            return widths.size();
        };
        QVERIFY2(intermediateWidths(opening, openWidth, closedWidth) >= 3,
                 "Opening must resize the actual editor viewport through intermediate widths");
        QCOMPARE(motion->drawerSnapshotBytes(), 0);
        window.resizeDocks({context->dockWidget()}, {420}, Qt::Horizontal);
        QTest::qWait(30);
        const int preferred = context->dockWidget()->width();
        const int beforeClose = editor->viewport()->width();
        action->trigger();
        QVERIFY(motion->isDrawerAnimating());
        QCOMPARE(context->captureState().dockWidth, preferred);
        const auto closing = sample("close");
        QVERIFY(checkContinuous(closing, false));
        QVERIFY2(intermediateWidths(closing, beforeClose, closedWidth) >= 3,
                 "Closing must resize the actual editor viewport through intermediate widths");
        QCOMPARE(editor->viewport()->width(), closedWidth);
        action->trigger(); QTest::qWait(45);
        const int midWidth = editor->viewport()->width();
        action->trigger();
        QVERIFY(qAbs(editor->viewport()->width() - midWidth) <= 1);
        QTest::qWait(20); action->trigger();
        QTRY_VERIFY(!motion->isDrawerAnimating());
        QCOMPARE(context->dockWidget()->width(), preferred);
        QVERIFY(context->dockWidget()->minimumWidth() < context->dockWidget()->maximumWidth());
        // Restore a kept resource; transient tools and More intentionally do not
        // persist a section in the workspace state.
        const auto resource = LiveInsightsContextProvider::resourceForKind(LiveInsightKind::Module, files.path());
        QVERIFY(context->openResource(resource, {ContextSurface::Docked, ContextPersistence::Kept, ContextBinding::Global}));
        motion->finishDrawerAnimation();
        window.resizeDocks({context->dockWidget()}, {preferred}, Qt::Horizontal); QTest::qWait(20);
        const auto retainedState = context->captureState();
        QVERIFY(!retainedState.dockSections.isEmpty());
        QCOMPARE(retainedState.dockWidth, preferred);
        context->setDockVisible(false); QTest::qWait(30);
        context->restoreState(retainedState);
        QVERIFY(!motion->isDrawerAnimating()); QVERIFY(context->dockVisible());
        QVERIFY(context->dockWidget()->minimumWidth() < context->dockWidget()->maximumWidth());
        QTRY_COMPARE(context->dockWidget()->width(), preferred);
        context->setDockVisible(false); QTest::qWait(30);
        window.resize(1360, 820);
        QTRY_VERIFY(!motion->isDrawerAnimating());
        QVERIFY(!context->dockVisible());
        context->setDockVisible(true); QTRY_VERIFY(!motion->isDrawerAnimating());
        QCOMPARE(context->dockWidget()->width(), preferred);
        context->setDockVisible(false); QTest::qWait(30);
        context->dockWidget()->setFloating(true);
        QTRY_VERIFY(!motion->isDrawerAnimating());
        QVERIFY(context->dockWidget()->minimumWidth() < context->dockWidget()->maximumWidth());
        QTRY_COMPARE(context->dockWidget()->width(), preferred);
        QCOMPARE(context->captureState().dockWidth, preferred);
        context->dockWidget()->setFloating(false);
        context->setDockVisible(true); motion->finishDrawerAnimation();
        QCOMPARE(context->captureState().dockWidth, preferred);
        QTRY_COMPARE(context->dockWidget()->width(), preferred);
        window.resizeDocks({context->dockWidget()}, {preferred + 40}, Qt::Horizontal);
        QTRY_COMPARE(context->captureState().dockWidth, preferred + 40);
        window.resizeDocks({context->dockWidget()}, {preferred}, Qt::Horizontal);
        QTRY_COMPARE(context->captureState().dockWidth, preferred);
        QCOMPARE(editor->toPlainText(), original);
        ApplicationThemeManager::instance().setAnimationsEnabled(false);
        context->setDockVisible(false); QVERIFY(!motion->isDrawerAnimating()); QVERIFY(!context->dockVisible());
        context->setDockVisible(true); QVERIFY(!motion->isDrawerAnimating()); QVERIFY(context->dockVisible());
        QCOMPARE(context->dockWidget()->width(), preferred);
#endif
    }

    void sidebarFrameClocksStopWhenIdleAndAnimationsAreDisabled() {
#ifdef ZEROSLACK_ENABLE_ELA
        auto& theme = ApplicationThemeManager::instance();
        const bool previous = theme.animationsEnabled();
        const auto restore = qScopeGuard([&] { theme.setAnimationsEnabled(previous); });
        theme.setAnimationsEnabled(false);
        Harness h;
        h.navigation->setExpanded(false);
        h.context->setDockVisible(false);
        QTRY_VERIFY(!h.navigation->isAnimating());
        auto* right = h.window.findChild<ElaDrawerArea*>("contextSidebarDrawer");
        QVERIFY(right); QVERIFY(!right->isDrawerAnimating());
        const auto clocks = h.window.findChildren<QTimer*>("elaSidebarFrameTimer");
        QVERIFY(clocks.size() >= 2);
        for (auto* clock : clocks) QVERIFY(!clock->isActive());
        theme.setAnimationsEnabled(true);
        h.navigation->setExpanded(true);
        h.context->setDockVisible(true);
        QVERIFY(h.navigation->isAnimating()); QVERIFY(right->isDrawerAnimating());
        QTest::qWait(35);
        theme.setAnimationsEnabled(false);
        h.navigation->setExpanded(true);
        h.context->setDockVisible(true);
        QTRY_VERIFY(!h.navigation->isAnimating());
        QVERIFY(!right->isDrawerAnimating());
        QVERIFY(h.navigation->isExpanded()); QVERIFY(h.context->dockVisible());
        for (auto* clock : clocks) QVERIFY(!clock->isActive());
        h.window.resize(760, 780);
        h.navigation->setExpanded(false);
        h.navigation->setExpanded(true);
        QTRY_VERIFY(!h.navigation->isAnimating());
        QVERIFY(h.navigation->isExpanded());
        for (auto* clock : clocks) QVERIFY(!clock->isActive());
#endif
    }

    void navigationFrameClockReversesAndStopsOnOwnerDestruction() {
#ifdef ZEROSLACK_ENABLE_ELA
        auto& theme = ApplicationThemeManager::instance();
        const bool previous = theme.animationsEnabled();
        const auto restore = qScopeGuard([&] { theme.setAnimationsEnabled(previous); });
        theme.setAnimationsEnabled(true);
        auto h = std::make_unique<Harness>();
        h->navigation->setExpanded(false, false);
        QTRY_VERIFY(!h->navigation->isAnimating());
        h->navigation->setExpanded(true);
        QVERIFY(h->navigation->isAnimating());
        QTest::qWait(35);
        const int beforeReverse = h->navigation->dock()->width();
        h->navigation->setExpanded(false);
        QVERIFY(h->navigation->isAnimating());
        QVERIFY(qAbs(h->navigation->dock()->width() - beforeReverse) <= 1);
        QTRY_VERIFY(!h->navigation->isAnimating());
        QVERIFY(!h->navigation->isExpanded());
        const auto clocks = h->window.findChildren<QTimer*>("elaSidebarFrameTimer");
        QVERIFY(!clocks.isEmpty());
        for (auto* clock : clocks) QVERIFY(!clock->isActive());
        h->navigation->setExpanded(true);
        QVERIFY(h->navigation->isAnimating());
        QList<QPointer<QTimer>> retained;
        for (auto* clock : clocks) retained.append(clock);
        h.reset();
        for (const auto& clock : retained) QVERIFY(clock.isNull());
        QTest::qWait(280);
#endif
    }

    void nativeDockTargetsUseQtWithoutSnapshots() {
#ifdef ZEROSLACK_ENABLE_ELA
        Harness h;
        QVERIFY(!h.window.findChild<ContextDockTransition*>());
        ContextFloatingWindow source(&h.window, h.editor);
        ContextResource resource;
        resource.providerId = "motion-test"; resource.resourceId = "preview";
        resource.uri = QUrl("motion:preview");
        auto* view = new QLabel("Retained content");
        source.setView(resource, view);
        QTest::qWait(240);
        source.beginNativeDockDrag(source.mapToGlobal(QPoint(24, 16)));
        QVERIFY(source.isDockDragging());
        QTest::keyClick(&source, Qt::Key_Escape);
        QVERIFY(!source.isDockDragging());
        QVERIFY(source.isFloating());
        QCOMPARE(source.view(), view);
        QVERIFY(!h.editor->document()->isModified());
#endif
    }

    void floatingTransferRetainsEditorAndSettles_data() {
        QTest::addColumn<bool>("bottom");
        QTest::addColumn<bool>("existing");
        QTest::newRow("hidden-side") << false << false;
        QTest::newRow("existing-side") << false << true;
        QTest::newRow("hidden-bottom") << true << false;
        QTest::newRow("existing-bottom") << true << true;
    }
    void floatingTransferRetainsEditorAndSettles() {
        QFETCH(bool, bottom); QFETCH(bool, existing);
        Harness h;
        auto* dock = h.context->dockHost();
        auto* side = h.context->dockWidget();
        auto* bottomDock = h.context->bottomDockWidget();
        if (existing) {
            if (bottom) {
                for (const auto& key : h.keys) dock->moveResourceToArea(key, true);
                h.drawer->setBottomCompanionVisible(true);
                h.drawer->setPanelHeight(h.drawer->activeBottomPanelId(), 250);
            } else h.context->setDockVisible(true);
        }
        QApplication::processEvents();
        ContextResource resource;
        resource.providerId = "motion-test"; resource.resourceId = "incoming";
        resource.title = "Incoming editor"; resource.uri = QUrl("motion:incoming");
        const auto key = resource.stableKey();
        ContextFloatingWindow source(&h.window, h.editor);
        auto* editor = new Editor;
        QStringList lines;
        for (int i = 0; i < 140; ++i) lines.append(QString("logic retained_%1;").arg(i));
        editor->setPlainText(lines.join('\n'));
        editor->moveCursor(QTextCursor::End);
        editor->insertPlainText("\n// retained undo");
        source.setView(resource, editor);
        source.setActionsAvailable(true, false);
        source.move(h.window.mapToGlobal(QPoint(170, 95)));
        source.show(); QApplication::processEvents();
        const QString text = editor->toPlainText();
        const auto cursor = editor->textCursor().position();
        const int scroll = editor->verticalScrollBar()->value();
        const int undo = editor->document()->availableUndoSteps();
#ifdef ZEROSLACK_ENABLE_ELA
        bool transferred = false;
        connect(&source, &ContextFloatingWindow::nativeDocked, &h.window, [&](Qt::DockWidgetArea area) {
            auto* view = source.takeView();
            transferred = dock->addResource(resource, view);
            dock->moveResourceToArea(key, area == Qt::BottomDockWidgetArea, 0);
            auto* target = bottom ? bottomDock : side;
            if (bottom) {
                h.drawer->setBottomCompanionVisible(true);
                h.drawer->setPanelHeight(h.drawer->activeBottomPanelId(), 280);
            } else {
                target->show();
                h.window.resizeDocks({target}, {340}, Qt::Horizontal);
            }
        });
        h.window.addDockWidget(bottom ? Qt::BottomDockWidgetArea : Qt::RightDockWidgetArea, &source);
        source.setFloating(false);
        QTRY_VERIFY(transferred);
#else
        QVERIFY(dock->addResource(resource, source.takeView()));
        dock->moveResourceToArea(key, bottom, 0);
        if (bottom) h.drawer->setBottomCompanionVisible(true);
        else side->show();
#endif
        QVERIFY(!h.window.findChild<ContextDockTransition*>());
        QVERIFY(!source.hasResource()); QVERIFY(!source.isVisible());
        QCOMPARE(dock->viewForResource(key), editor);
        QCOMPARE(dock->isBottomResource(key), bottom);
        QApplication::processEvents();
        QCOMPARE(editor->toPlainText(), text);
        QCOMPARE(editor->textCursor().position(), cursor);
        QCOMPARE(editor->verticalScrollBar()->value(), qMin(scroll, editor->verticalScrollBar()->maximum()));
        QCOMPARE(editor->document()->availableUndoSteps(), undo);
        QVERIFY(editor->document()->isModified());
        QVERIFY(editor->isVisible());
    }

    void floatingTransferFailureAndInterruption() {
#ifdef ZEROSLACK_ENABLE_ELA
        Harness h;
        ContextResource resource;
        resource.providerId = "motion-test"; resource.resourceId = "interrupted";
        resource.uri = QUrl("motion:interrupted");
        ContextFloatingWindow source(&h.window, h.editor);
        auto* content = new QLabel("Retained content");
        source.setView(resource, content);
        QTest::qWait(240);
        source.beginNativeDockDrag(source.mapToGlobal(QPoint(24, 16)));
        QVERIFY(source.isDockDragging());
        source.setActionsAvailable(false, false);
        QVERIFY(!source.isDockDragging());
        QCOMPARE(source.view(), content);
        QVERIFY(source.isFloating()); QVERIFY(source.isVisible());
        source.setActionsAvailable(true, false);
        source.beginNativeDockDrag(source.mapToGlobal(QPoint(24, 16)));
        auto* retained = source.takeView();
        QVERIFY(!source.isDockDragging());
        QVERIFY(!source.hasResource());
        QCOMPARE(retained, content);
        delete retained;
#endif
    }

    void nativeCompositionAcceptsTransparentTransferLayers() {
        if (QGuiApplication::platformName() != "windows") QSKIP("Windows hidden-HWND check");
        QWidget surface(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus
            | Qt::WindowTransparentForInput);
        surface.setAttribute(Qt::WA_TranslucentBackground);
        surface.resize(240, 160);
        surface.winId();
        auto* focused = QApplication::focusWidget();
        auto* active = QApplication::activeWindow();
        QImage image(QSize(100, 80) * surface.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(surface.devicePixelRatioF());
        image.fill(QColor(50, 120, 210, 160));
        auto layer = PanelMotionLayer::stationary(image);
        layer.closedOpacity = 0;
        layer.opacityStart = .97;
        layer.openPosition = QPointF(70, 40);
        layer.openClip = QSizeF(80, 60);
        NativePanelComposition native;
        native.warmUp();
        QTRY_VERIFY_WITH_TIMEOUT(native.prepare(&surface, surface.rect(), {layer}), 5000);
        QVERIFY(native.animate({layer}, 0, 1, ContextDockTransition::duration));
        QTest::qWait(ContextDockTransition::duration + 25);
        native.clear();
        QVERIFY(!surface.isVisible());
        QCOMPARE(QApplication::focusWidget(), focused);
        QCOMPARE(QApplication::activeWindow(), active);
    }

    void destroyingOwnerDuringFloatingTransfer() {
#ifdef ZEROSLACK_ENABLE_ELA
        auto* window = new MainWindow;
        window->resize(1200, 780);
        window->show();
        auto* source = new ContextFloatingWindow(window, window->centralWidget());
        ContextResource resource;
        resource.providerId = "motion-test"; resource.resourceId = "owner-close";
        resource.uri = QUrl("motion:owner-close");
        source->setView(resource, new QLabel("Content"));
        QTest::qWait(240);
        source->beginNativeDockDrag(source->mapToGlobal(QPoint(24, 16)));
        QVERIFY(source->isDockDragging());
        QPointer<ContextFloatingWindow> retained = source;
        delete window;
        QVERIFY(retained.isNull());
        QTest::qWait(250);
#endif
    }

    void drawerReversesWithoutMovingButtonBar() {
        Harness h;
#ifdef ZEROSLACK_ENABLE_ELA
        {
        auto* motion = h.window.findChild<ElaDrawerArea*>("bottomPanelDrawer");
        QVERIFY(motion);
        const auto bar = inWindow(h.drawer->buttonBar(), &h.window);
        auto* button = h.drawer->buttonForPanel("problems");
        QTest::mouseClick(button, Qt::LeftButton);
        QVERIFY(motion->isDrawerAnimating());
        QCOMPARE(motion->drawerSnapshotBytes(), 0);
        QApplication::processEvents();
        const auto editorGeometry = h.editor->geometry();
        const int resizes = h.editor->resizes;
        QTest::qWait(55);
        QVERIFY(h.editor->geometry().height() < editorGeometry.height());
        QVERIFY(h.editor->resizes > resizes);
        QCOMPARE(inWindow(h.drawer->buttonBar(), &h.window), bar);
        const auto progress = motion->drawerProgress();
        QTest::mouseClick(button, Qt::LeftButton);
        QVERIFY(motion->isDrawerAnimating());
        QVERIFY(qAbs(motion->drawerProgress() - progress) < .08);
        QVERIFY(h.drawer->isBottomCollapsed());
        QTRY_VERIFY(!motion->isDrawerAnimating());
        QCOMPARE(inWindow(h.drawer->buttonBar(), &h.window), bar);
        QCOMPARE(motion->drawerSnapshotBytes(), 0);
        h.drawer->restorePanel("activity");
        QVERIFY(motion->isDrawerAnimating());
        QTest::keyClicks(h.editor, "x");
        QVERIFY(!motion->isDrawerAnimating());
        QVERIFY(h.editor->toPlainText().startsWith("xmodule"));
        const auto state = h.drawer->layoutState();
        h.drawer->setBottomCollapsed(true);
        h.drawer->restoreLayoutState(state);
        QVERIFY(!motion->isDrawerAnimating());
        QVERIFY(!h.drawer->isBottomCollapsed());
        h.drawer->setBottomCollapsed(true);
        h.drawer->setAnimationsEnabled(false);
        QVERIFY(!motion->isDrawerAnimating());
        h.drawer->setBottomCollapsed(false);
        QVERIFY(!motion->isDrawerAnimating());
        return;
        }
#endif
        const auto bar = inWindow(h.drawer->buttonBar(), &h.window);
        h.drawer->restorePanel("problems");
        QVERIFY(!h.drawer->isBottomCollapsed());
        QCOMPARE(inWindow(h.drawer->buttonBar(), &h.window), bar);
        h.drawer->setBottomCollapsed(true);
        QVERIFY(h.drawer->isBottomCollapsed());
        QCOMPARE(inWindow(h.drawer->buttonBar(), &h.window), bar);
        h.drawer->restorePanel("activity");
        QTest::keyClicks(h.editor, "x");
        QVERIFY(h.editor->toPlainText().startsWith("xmodule"));
        const auto state = h.drawer->layoutState();
        h.drawer->setBottomCollapsed(true);
        h.drawer->restoreLayoutState(state);
        QVERIFY(!h.drawer->isBottomCollapsed());
        h.drawer->setAnimationsEnabled(false);
        h.drawer->setBottomCollapsed(true);
        QVERIFY(h.drawer->isBottomCollapsed());
    }

    void bottomHeightSurvivesWindowResizing_data() {
        QTest::addColumn<bool>("companion");
        QTest::newRow("original-panel") << false;
        QTest::newRow("with-bottom-content") << true;
    }

    void bottomHeightSurvivesWindowResizing() {
#ifdef ZEROSLACK_ENABLE_ELA
        QFETCH(bool, companion);
        MainWindow window;
        window.resize(1280, 800); window.show(); QTest::qWait(100);
        PanelLayoutController* panels = nullptr;
        for (auto* child : window.children())
            if (auto* value = dynamic_cast<PanelLayoutController*>(child)) panels = value;
        QVERIFY(panels);
        auto* context = window.findChild<ContextWorkspaceController*>(); QVERIFY(context);
        auto* motion = window.findChild<ElaDrawerArea*>("bottomPanelDrawer"); QVERIFY(motion);
        if (companion) {
            ContextResource resource;
            resource.providerId = "motion-test"; resource.resourceId = "bottom-boundary";
            resource.uri = QUrl("motion:bottom-boundary"); resource.title = "Retained bottom content";
            QVERIFY(context->dockHost()->addResource(resource, new QLabel("Companion content")));
            QVERIFY(context->dockHost()->moveResourceToArea(resource.stableKey(), Qt::BottomDockWidgetArea));
            panels->setBottomCompanionVisible(true);
        }
        panels->setAnimationsEnabled(false);
        QVERIFY(panels->restorePanel("problems"));
        QVERIFY(panels->setPanelHeight("problems", 400));
        QTest::qWait(50);
        const QSize normalSize = window.size();
        auto* primary = panels->drawerContent(); QVERIFY(primary);
        const auto preferred = [&] { return panels->layoutState().bottomPanelHeights.value("problems"); };
        const auto contentIsSynchronized = [&] {
            return !companion || primary->height() == context->dockHost()->bottomWidget()->height();
        };
        QCOMPARE(primary->height(), 400);
        window.resize(1280, 500); QTest::qWait(80);
        QVERIFY(panels->maximumContentHeight() < 400);
        QCOMPARE(primary->height(), panels->maximumContentHeight());
        QVERIFY(contentIsSynchronized());
        QCOMPARE(preferred(), 400);
        const auto constrainedState = panels->layoutState();
        panels->restoreLayoutState(constrainedState);
        window.resize(normalSize);
        QTRY_COMPARE(primary->height(), 400);
        QCOMPARE(preferred(), 400);

        window.showMaximized(); QTRY_VERIFY(window.isMaximized()); QTest::qWait(80);
        QCOMPARE(primary->height(), qMin(400, panels->maximumContentHeight()));
        QCOMPARE(preferred(), 400); QVERIFY(contentIsSynchronized());
        window.showNormal(); window.resize(normalSize);
        QTRY_COMPARE(primary->height(), 400);

        panels->setBottomCollapsed(true);
        window.resize(1280, 500); QTest::qWait(80);
        panels->setBottomCollapsed(false);
        QCOMPARE(primary->height(), panels->maximumContentHeight());
        QCOMPARE(preferred(), 400); QVERIFY(contentIsSynchronized());
        window.resize(normalSize); QTRY_COMPARE(primary->height(), 400);

        panels->setBottomCollapsed(true);
        panels->setAnimationsEnabled(true);
        panels->setBottomCollapsed(false); QTest::qWait(50);
        QVERIFY(motion->isDrawerAnimating());
        QVERIFY(primary->height() > 0 && primary->height() < 400);
        QVERIFY(contentIsSynchronized());
        window.resize(1280, 500); QTest::qWait(80);
        QVERIFY(!motion->isDrawerAnimating());
        QCOMPARE(primary->height(), panels->maximumContentHeight());
        QCOMPARE(preferred(), 400); QVERIFY(contentIsSynchronized());
        window.resize(normalSize); QTRY_COMPARE(primary->height(), 400);
        panels->setBottomCollapsed(true); QTest::qWait(50);
        QVERIFY(motion->isDrawerAnimating()); QVERIFY(contentIsSynchronized());
        window.resize(1280, 500); QTest::qWait(80);
        QVERIFY(!motion->isDrawerAnimating()); QVERIFY(panels->isBottomCollapsed());
        QCOMPARE(preferred(), 400);
        window.resize(normalSize); panels->setBottomCollapsed(false);
        QTRY_VERIFY(!motion->isDrawerAnimating());
        QCOMPARE(primary->height(), 400); QVERIFY(contentIsSynchronized());

        // A user resize is an intentional preference change, unlike a window resize.
        QWidget* handle = panels->resizeHandle();
        const QPoint center = handle->rect().center();
        QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, center);
        QTest::mouseMove(handle, center + QPoint(0, 80));
        QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, center);
        QCOMPARE(preferred(), 320);
        QCOMPARE(primary->height(), 320); QVERIFY(contentIsSynchronized());
        window.resize(1280, 500); QTest::qWait(80);
        window.resize(normalSize); QTest::qWait(80);
        QCOMPARE(preferred(), 320); QCOMPARE(primary->height(), 320);
#else
        QSKIP("Maintained Ela live drawer behavior");
#endif
    }

    void rightSidebarPreservesViewsAndCanReverse() {
        Harness h;
#ifdef ZEROSLACK_ENABLE_ELA
        {
        auto* motion = h.window.findChild<ElaDrawerArea*>("contextSidebarDrawer");
        QVERIFY(motion);
        auto* view = h.context->dockHost()->viewForResource(h.keys[0]);
        QVERIFY(h.context->setDockVisible(true));
        QVERIFY(motion->isDrawerAnimating());
        QApplication::processEvents();
        const int resizes = h.editor->resizes;
        const auto geometry = h.editor->geometry();
        QTest::qWait(55);
        QVERIFY(h.editor->geometry() != geometry);
        QVERIFY(h.editor->resizes > resizes);
        const auto progress = motion->drawerProgress();
        QVERIFY(h.context->setDockVisible(false));
        QVERIFY(motion->isDrawerAnimating());
        QVERIFY(qAbs(motion->drawerProgress() - progress) < .08);
        QTRY_VERIFY(!motion->isDrawerAnimating());
        QVERIFY(!h.context->dockVisible());
        for (int i = 0; i < 4; ++i) {
            QVERIFY(h.context->setDockVisible(i % 2 == 0));
            QTest::qWait(30);
        }
        QTRY_VERIFY(!motion->isDrawerAnimating());
        QVERIFY(!h.context->dockVisible());
        QCOMPARE(h.context->dockHost()->viewForResource(h.keys[0]), view);
        h.context->setDockVisible(true);
        motion->finishDrawerAnimation();
        h.context->dockWidget()->close();
        QVERIFY(motion->isDrawerAnimating());
        QTRY_VERIFY(!motion->isDrawerAnimating());
        QVERIFY(!h.context->dockVisible());
        QCOMPARE(motion->drawerSnapshotBytes(), 0);
        QVERIFY(!h.editor->document()->isModified());
        return;
        }
#endif
        auto* view = h.context->dockHost()->viewForResource(h.keys[0]);
        for (bool open : {true, false, true, false, true}) {
            QVERIFY(h.context->setDockVisible(open));
            QCOMPARE(h.context->dockVisible(), open);
        }
        QCOMPARE(h.context->dockHost()->viewForResource(h.keys[0]), view);
        h.context->dockWidget()->close();
        QVERIFY(!h.context->dockVisible());
        QVERIFY(!h.editor->document()->isModified());
    }

    void sectionsRetainContentAndSettleBeforeStructuralChanges() {
        Harness h;
#ifdef ZEROSLACK_ENABLE_ELA
        {
        h.context->setDockVisible(true);
        h.window.findChild<ElaDrawerArea*>("contextSidebarDrawer")->finishDrawerAnimation();
        QTest::qWait(40);
        auto* dock = h.context->dockHost();
        auto* view = dock->viewForResource(h.keys[0]);
        auto* motion = dock->sectionWidget(h.keys[0])->findChild<ElaDrawerArea*>("contextSectionDrawer");
        QVERIFY(motion);
        const auto size = dock->sectionWidget(h.keys[0])->size();
        QVERIFY(dock->setSectionCollapsed(h.keys[0], true));
        QVERIFY(motion->isDrawerAnimating());
        QVERIFY(dock->isSectionCollapsed(h.keys[0]));
        const auto after = dock->sectionWidget(h.keys[1])->geometry();
        QTest::qWait(55);
        QCOMPARE(dock->sectionWidget(h.keys[1])->geometry(), after);
        const auto progress = motion->drawerProgress();
        QVERIFY(dock->setSectionCollapsed(h.keys[0], false));
        QVERIFY(motion->isDrawerAnimating());
        QVERIFY(qAbs(motion->drawerProgress() - progress) < .08);
        QTRY_VERIFY(!motion->isDrawerAnimating());
        QCOMPARE(dock->viewForResource(h.keys[0]), view);
        QCOMPARE(dock->sectionWidget(h.keys[0])->size(), size);
        QVERIFY(view->isVisible());
        dock->setSectionCollapsed(h.keys[0], true);
        dock->setSectionHeight(h.keys[1], 200);
        QVERIFY(!motion->isDrawerAnimating());
        dock->setSectionCollapsed(h.keys[0], false);
        dock->moveResource(h.keys[1], 0);
        QVERIFY(!motion->isDrawerAnimating());
        dock->setSectionCollapsed(h.keys[0], true);
        QPointer<ElaDrawerArea> retained = motion;
        dock->removeResource(h.keys[0]);
        QVERIFY(!retained || !retained->isDrawerAnimating());
        return;
        }
#endif
        h.context->setDockVisible(true);
        auto* dock = h.context->dockHost();
        auto* view = dock->viewForResource(h.keys[0]);
        for (bool collapsed : {true, false, true, false}) {
            QVERIFY(dock->setSectionCollapsed(h.keys[0], collapsed));
            QCOMPARE(dock->isSectionCollapsed(h.keys[0]), collapsed);
        }
        QCOMPARE(dock->viewForResource(h.keys[0]), view);
        QVERIFY(view->isVisible());
        dock->setSectionHeight(h.keys[1], 200);
        dock->moveResource(h.keys[1], 0);
        dock->removeResource(h.keys[0]);
        QVERIFY(!dock->containsResource(h.keys[0]));
    }

    void viewportBackgroundIsCapturedInsteadOfGuessed() {
        Harness h;
#ifdef ZEROSLACK_ENABLE_ELA
        {
        auto* dock = h.context->dockHost();
        dock->removeResource(h.keys[1]);
        h.context->setDockVisible(true);
        h.window.findChild<ElaDrawerArea*>("contextSidebarDrawer")->finishDrawerAnimation();
        QTest::qWait(40);
        auto* motion = dock->sectionWidget(h.keys[0])->findChild<ElaDrawerArea*>("contextSectionDrawer");
        QVERIFY(motion);
        auto* viewport = dock->findChild<QScrollArea*>()->viewport();
        auto palette = viewport->palette();
        palette.setColor(QPalette::Base, QColor(73, 137, 192));
        viewport->setPalette(palette);
        viewport->setBackgroundRole(QPalette::Base);
        viewport->setAutoFillBackground(true);
        dock->setSectionCollapsed(h.keys[0], true);
        QTest::qWait(40);
        QVERIFY(motion->isDrawerAnimating());
        QCOMPARE(viewport->palette().color(QPalette::Base), QColor(73, 137, 192));
        QVERIFY(motion->drawerSnapshotBytes() <= qint64(motion->width() * motion->devicePixelRatioF())
            * qCeil(motion->height() * motion->devicePixelRatioF()) * 4);
        motion->finishDrawerAnimation();
        QVERIFY(!dock->viewForResource(h.keys[0])->isVisible());
        return;
        }
#endif
        auto* dock = h.context->dockHost();
        h.context->setDockVisible(true);
        auto* viewport = dock->findChild<QScrollArea*>()->viewport();
        auto palette = viewport->palette();
        palette.setColor(QPalette::Base, QColor(73,137,192));
        viewport->setPalette(palette);
        viewport->setBackgroundRole(QPalette::Base);
        viewport->setAutoFillBackground(true);
        dock->setSectionCollapsed(h.keys[0], true);
        QCOMPARE(viewport->palette().color(QPalette::Base), QColor(73,137,192));
        QVERIFY(!dock->viewForResource(h.keys[0])->isVisible());
    }

    void nativeNavigationAndOtherPanelsCoexistSafely() {
        Harness h;
#ifdef ZEROSLACK_ENABLE_ELA
        {
        h.navigation->setExpanded(false);
        QVERIFY(h.navigation->isAnimating());
        h.context->setDockVisible(true);
        auto* side = h.window.findChild<ElaDrawerArea*>("contextSidebarDrawer");
        auto* bottom = h.window.findChild<ElaDrawerArea*>("bottomPanelDrawer");
        QVERIFY(side->isDrawerAnimating());
        h.drawer->restorePanel("problems");
        QVERIFY(bottom->isDrawerAnimating());
        side->finishDrawerAnimation();
        h.context->dockHost()->setSectionCollapsed(h.keys[0], true);
        auto* section = h.context->dockHost()->sectionWidget(h.keys[0])->findChild<ElaDrawerArea*>("contextSectionDrawer");
        QVERIFY(section->isDrawerAnimating());
        h.window.resize(1250, 810);
        QTRY_VERIFY(!h.navigation->isAnimating());
        QTRY_VERIFY(!bottom->isDrawerAnimating() && !section->isDrawerAnimating());
        QVERIFY(h.drawer->isPanelOpen("problems"));
        QVERIFY(h.context->dockVisible());
        QVERIFY(!h.editor->document()->isModified());
        return;
        }
#endif
        h.navigation->setExpanded(false);
        QVERIFY(!h.navigation->isAnimating());
        h.context->setDockVisible(true);
        h.drawer->restorePanel("problems");
        h.context->dockHost()->setSectionCollapsed(h.keys[0], true);
        h.window.resize(1250, 810);
        QVERIFY(h.drawer->isPanelOpen("problems"));
        QVERIFY(h.context->dockVisible());
        QVERIFY(!h.editor->document()->isModified());
    }

    void globalAnimationPolicySettlesEveryOwner() {
#ifdef ZEROSLACK_ENABLE_ELA
        Harness h;
        auto& policy = ApplicationThemeManager::instance();
        const auto restore = qScopeGuard([&] { policy.setAnimationsEnabled(true); });
        QStackedWidget* stack = nullptr;
        auto* surface = UiControls::pageStack(stack, h.editor);
        stack->addWidget(new QLabel("one")); stack->addWidget(new QLabel("two"));
        surface->setGeometry(10,10,200,160); surface->show();
        auto* pages = qobject_cast<ElaCentralStackedWidget*>(surface); QVERIFY(pages);
        h.context->setDockVisible(true);
        auto* side = h.window.findChild<ElaDrawerArea*>("contextSidebarDrawer");
        side->finishDrawerAnimation();
        auto* sections = h.context->dockHost();
        auto* section = sections->sectionWidget(h.keys[0])->findChild<ElaDrawerArea*>("contextSectionDrawer");
        auto* bottom = h.window.findChild<ElaDrawerArea*>("bottomPanelDrawer");
        h.navigation->setExpanded(false);
        h.drawer->restorePanel("problems");
        sections->setSectionCollapsed(h.keys[0], true);
        UiControls::selectPage(stack, 1, true);
        QVERIFY(h.navigation->isAnimating()); QVERIFY(bottom->isDrawerAnimating());
        QVERIFY(section->isDrawerAnimating()); QVERIFY(pages->isStackSwitching());
        policy.setAnimationsEnabled(false);
        QVERIFY(!h.navigation->isAnimating()); QVERIFY(!bottom->isDrawerAnimating());
        QVERIFY(!section->isDrawerAnimating()); QVERIFY(!pages->isStackSwitching());
        QCOMPARE(stack->currentIndex(),1); QVERIFY(stack->currentWidget()->isVisible());
        QVERIFY(h.navigation->dock()->isHidden()); QVERIFY(h.drawer->isPanelOpen("problems"));
        QVERIFY(sections->isSectionCollapsed(h.keys[0]));
        h.context->setDockVisible(false); h.context->setDockVisible(true);
        QVERIFY(!side->isDrawerAnimating());
        sections->setSectionCollapsed(h.keys[0], false);
        QVERIFY(!section->isDrawerAnimating()); QVERIFY(sections->viewForResource(h.keys[0])->isVisible());
        UiControls::selectPage(stack,0,true); QVERIFY(!pages->isStackSwitching());
        policy.setAnimationsEnabled(true);
        h.context->setDockVisible(false); QVERIFY(side->isDrawerAnimating());
        policy.setAnimationsEnabled(false); QVERIFY(!side->isDrawerAnimating());
        QVERIFY(!h.context->dockVisible());
        policy.setAnimationsEnabled(true);
        sections->setSectionCollapsed(h.keys[0],true,false);
        QVERIFY(!section->isDrawerAnimating());
        UiControls::selectPage(stack,1,true); QVERIFY(pages->isStackSwitching());
        UiControls::selectPage(stack,1,false); QVERIFY(!pages->isStackSwitching());
        const auto limits = qMakePair(h.context->dockWidget()->minimumWidth(), h.context->dockWidget()->maximumWidth());
        QTest::qWait(350);
        QCOMPARE(limits, qMakePair(h.context->dockWidget()->minimumWidth(), h.context->dockWidget()->maximumWidth()));
        QVERIFY(stack->currentWidget()->isVisible());
#endif
    }

    void nativePanelOwnerLifetime() {
        for (int scene = 0; scene < 3; ++scene) {
            auto* transient = new Harness;
            if (scene == 0) transient->drawer->restorePanel("problems");
            else {
                transient->context->setDockVisible(true);
                if (scene == 2) {
#ifdef ZEROSLACK_ENABLE_ELA
                    transient->window.findChild<ElaDrawerArea*>("contextSidebarDrawer")->finishDrawerAnimation();
#endif
                    transient->context->dockHost()->setSectionCollapsed(transient->keys[0], true);
                }
            }
#ifdef ZEROSLACK_ENABLE_ELA
            bool active = false;
            for (auto* motion : transient->window.findChildren<ElaDrawerArea*>()) active |= motion->isDrawerAnimating();
            QVERIFY(active);
#endif
            delete transient;
        }
        QTest::qWait(300);
    }
};

int main(int argc, char** argv) {
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    QTemporaryDir profile; if (!profile.isValid()) return 2;
    QCoreApplication::setApplicationName("ZeroSlack-Panel-Motion-Test");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    if (!initializeUiStyleForTest()) return 3;
    ApplicationThemeManager::instance().applyToApplication();
    UiPanelMotionTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "ui_panel_motion_test.moc"
