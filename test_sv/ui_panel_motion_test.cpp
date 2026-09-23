#include "applicationthememanager.h"
#include "contextdockhost.h"
#include "contextdocktransition.h"
#include "contextfloatingwindow.h"
#include "contextworkspacecontroller.h"
#include "navigationpanecoordinator.h"
#include "nativepanelcomposition.h"
#include "panelcompositor.h"
#include "panellayoutcontroller.h"
#include "testuistyle.h"
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
    PanelCompositor* compositor;
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
        compositor = PanelCompositor::forWindow(&window);
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
        QVERIFY(!h.compositor->isActive());
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
                bottomDock->show();
                h.window.resizeDocks({bottomDock}, {250}, Qt::Vertical);
            } else h.context->setDockVisible(true);
        }
        h.compositor->settle(); QApplication::processEvents();
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
            target->show();
            h.window.resizeDocks({target}, {bottom ? 280 : 340}, bottom ? Qt::Vertical : Qt::Horizontal);
        });
        h.window.addDockWidget(bottom ? Qt::BottomDockWidgetArea : Qt::RightDockWidgetArea, &source);
        source.setFloating(false);
        QTRY_VERIFY(transferred);
#else
        QVERIFY(dock->addResource(resource, source.takeView()));
        dock->moveResourceToArea(key, bottom, 0);
        (bottom ? bottomDock : side)->show();
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
        auto* h = new Harness;
        auto* source = new ContextFloatingWindow(&h->window, h->editor);
        ContextResource resource;
        resource.providerId = "motion-test"; resource.resourceId = "owner-close";
        resource.uri = QUrl("motion:owner-close");
        source->setView(resource, new QLabel("Content"));
        QTest::qWait(240);
        source->beginNativeDockDrag(source->mapToGlobal(QPoint(24, 16)));
        QVERIFY(source->isDockDragging());
        QPointer<ContextFloatingWindow> retained = source;
        delete h;
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
        QVERIFY(motion->drawerSnapshotBytes() > 0);
        QApplication::processEvents();
        const auto editorGeometry = h.editor->geometry();
        const int resizes = h.editor->resizes;
        QTest::qWait(55);
        QCOMPARE(h.editor->geometry(), editorGeometry);
        QCOMPARE(h.editor->resizes, resizes);
        QCOMPARE(inWindow(h.drawer->buttonBar(), &h.window), bar);
        const auto progress = motion->drawerProgress();
        QTest::mouseClick(button, Qt::LeftButton);
        QVERIFY(motion->isDrawerAnimating());
        QVERIFY(qAbs(motion->drawerProgress() - progress) < .08);
        QVERIFY(h.drawer->isBottomCollapsed());
        QTRY_VERIFY(!motion->isDrawerAnimating());
        QCOMPARE(inWindow(h.drawer->buttonBar(), &h.window), bar);
        QCOMPARE(motion->drawerSnapshotBytes(), 0);
        QVERIFY(!h.compositor->isActive());
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
        QVERIFY(h.drawer->animationsEnabled());
        const auto bar = inWindow(h.drawer->buttonBar(), &h.window);
        auto* button = h.drawer->buttonForPanel("problems");
        QVERIFY(button);
        QTest::mouseClick(button, Qt::LeftButton);
        QVERIFY(h.compositor->isActiveFor(h.drawer));
        QVERIFY(h.compositor->snapshotBytes() > 0);
        const auto editorGeometry = h.editor->geometry();
        const int resizes = h.editor->resizes;
        QTest::qWait(55);
        QCOMPARE(h.editor->geometry(), editorGeometry);
        QCOMPARE(h.editor->resizes, resizes);
        QCOMPARE(inWindow(h.drawer->buttonBar(), &h.window), bar);
        const auto progress = h.compositor->progress();
        QTest::mouseClick(button, Qt::LeftButton);
        QVERIFY(h.compositor->isActiveFor(h.drawer));
        QVERIFY(qAbs(h.compositor->progress() - progress) < .08);
        QVERIFY(h.drawer->isBottomCollapsed());
        QTRY_VERIFY(!h.compositor->isActive());
        QCOMPARE(inWindow(h.drawer->buttonBar(), &h.window), bar);
        QCOMPARE(h.compositor->snapshotBytes(), 0);
        QVERIFY(!h.editor->document()->isModified());

        h.drawer->restorePanel("activity");
        QVERIFY(h.compositor->isActive());
        QTest::keyClicks(h.editor, "x");
        QVERIFY(!h.compositor->isActive());
        QVERIFY(h.editor->toPlainText().startsWith("xmodule"));
        const auto state = h.drawer->layoutState();
        h.drawer->setBottomCollapsed(true);
        h.drawer->restoreLayoutState(state);
        QVERIFY(!h.compositor->isActive());
        QVERIFY(!h.drawer->isBottomCollapsed());
        h.drawer->setBottomCollapsed(true);
        h.drawer->setAnimationsEnabled(false);
        QVERIFY(!h.compositor->isActive());
        h.drawer->setBottomCollapsed(false);
        QVERIFY(!h.compositor->isActive());
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
        QCOMPARE(h.editor->geometry(), geometry);
        QCOMPARE(h.editor->resizes, resizes);
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
        QVERIFY(!h.compositor->isActive());
        QVERIFY(!h.editor->document()->isModified());
        return;
        }
#endif
        auto* view = h.context->dockHost()->viewForResource(h.keys[0]);
        QVERIFY(h.context->setDockVisible(true));
        QVERIFY(h.compositor->isActiveFor(h.context->dockWidget()));
        const int resizes = h.editor->resizes;
        const auto geometry = h.editor->geometry();
        QTest::qWait(55);
        QCOMPARE(h.editor->geometry(), geometry);
        QCOMPARE(h.editor->resizes, resizes);
        const auto progress = h.compositor->progress();
        QVERIFY(h.context->setDockVisible(false));
        QVERIFY(h.compositor->isActiveFor(h.context->dockWidget()));
        QVERIFY(qAbs(h.compositor->progress() - progress) < .08);
        QTRY_VERIFY(!h.compositor->isActive());
        QVERIFY(!h.context->dockVisible());
        for (int i = 0; i < 4; ++i) {
            QVERIFY(h.context->setDockVisible(i % 2 == 0));
            QTest::qWait(30);
        }
        QTRY_VERIFY(!h.compositor->isActive());
        QVERIFY(!h.context->dockVisible());
        QCOMPARE(h.context->dockHost()->viewForResource(h.keys[0]), view);
        h.context->setDockVisible(true);
        h.compositor->settle();
        h.context->dockWidget()->close();
        QVERIFY(h.compositor->isActive());
        QTRY_VERIFY(!h.compositor->isActive());
        QVERIFY(!h.context->dockVisible());
        QCOMPARE(h.compositor->snapshotBytes(), 0);
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
        QVERIFY(!h.compositor->isActive());
        return;
        }
#endif
        h.context->setDockVisible(true); h.compositor->settle();
        QTest::qWait(40);
        auto* dock = h.context->dockHost();
        auto* view = dock->viewForResource(h.keys[0]);
        const auto size = dock->sectionWidget(h.keys[0])->size();
        QVERIFY(dock->setSectionCollapsed(h.keys[0], true));
        QVERIFY(h.compositor->isActiveFor(dock->sectionWidget(h.keys[0])));
        QVERIFY(dock->isSectionCollapsed(h.keys[0]));
        const auto after = dock->sectionWidget(h.keys[1])->geometry();
        QTest::qWait(55);
        QCOMPARE(dock->sectionWidget(h.keys[1])->geometry(), after);
        const auto progress = h.compositor->progress();
        QVERIFY(dock->setSectionCollapsed(h.keys[0], false));
        QVERIFY(h.compositor->isActive());
        QVERIFY(qAbs(h.compositor->progress() - progress) < .08);
        QTRY_VERIFY(!h.compositor->isActive());
        QCOMPARE(dock->viewForResource(h.keys[0]), view);
        QCOMPARE(dock->sectionWidget(h.keys[0])->size(), size);
        QVERIFY(view->isVisible());
        dock->setSectionCollapsed(h.keys[0], true);
        dock->setSectionHeight(h.keys[1], 200);
        QVERIFY(!h.compositor->isActive());
        dock->setSectionCollapsed(h.keys[0], false);
        dock->moveResource(h.keys[1], 0);
        QVERIFY(!h.compositor->isActive());
        dock->setSectionCollapsed(h.keys[0], true);
        dock->removeResource(h.keys[0]);
        QVERIFY(!h.compositor->isActive());
        QCOMPARE(h.compositor->snapshotBytes(), 0);
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
        QVERIFY(!h.compositor->isActive());
        QCOMPARE(viewport->palette().color(QPalette::Base), QColor(73, 137, 192));
        QVERIFY(motion->drawerSnapshotBytes() <= qint64(motion->width() * motion->devicePixelRatioF())
            * qCeil(motion->height() * motion->devicePixelRatioF()) * 4);
        motion->finishDrawerAnimation();
        QVERIFY(!dock->viewForResource(h.keys[0])->isVisible());
        return;
        }
#endif
        auto* dock = h.context->dockHost();
        dock->removeResource(h.keys[1]);
        h.context->setDockVisible(true); h.compositor->settle();
        QTest::qWait(40);
        auto* scroll = dock->findChild<QScrollArea*>();
        QVERIFY(scroll);
        auto* viewport = scroll->viewport();
        auto palette = viewport->palette();
        palette.setColor(QPalette::Window, Qt::black);
        palette.setColor(QPalette::Base, QColor(73, 137, 192));
        viewport->setPalette(palette);
        viewport->setBackgroundRole(QPalette::Base);
        viewport->setAutoFillBackground(true);
        dock->setSectionCollapsed(h.keys[0], true);
        QTest::qWait(40);
        QVERIFY(h.compositor->isActive());
        const auto actual = h.compositor->grab().toImage();
        const auto expected = viewport->grab().toImage();
        QCOMPARE(actual.size(), expected.size());
        const QPoint point(actual.width() / 2, actual.height() - 4);
        QCOMPARE(actual.pixelColor(point), expected.pixelColor(point));
        h.compositor->settle();
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
        QVERIFY(!h.compositor->isActive());
        QVERIFY(h.drawer->isPanelOpen("problems"));
        QVERIFY(h.context->dockVisible());
        QVERIFY(!h.editor->document()->isModified());
        return;
        }
#endif
        h.navigation->setExpanded(false);
        QVERIFY(h.navigation->isAnimating());
        h.context->setDockVisible(true);
        QVERIFY(h.compositor->isActiveFor(h.context->dockWidget()));
        h.drawer->restorePanel("problems");
        QVERIFY(h.compositor->isActiveFor(h.drawer));
        h.context->dockHost()->setSectionCollapsed(h.keys[0], true);
        QVERIFY(h.compositor->isActiveFor(h.context->dockHost()->sectionWidget(h.keys[0])));
        QCOMPARE(h.window.findChildren<PanelCompositor*>().size(), 1);
        h.window.resize(1250, 810);
        QTRY_VERIFY(!h.navigation->isAnimating());
        QVERIFY(!h.compositor->isActive());
        QCOMPARE(h.compositor->snapshotBytes(), 0);
        QVERIFY(h.drawer->isPanelOpen("problems"));
        QVERIFY(h.context->dockVisible());
        QVERIFY(!h.editor->document()->isModified());
    }

    void clippingAndOwnerLifetime() {
        Harness h;
        QSignalSpy finished(h.compositor, &PanelCompositor::finished);
        QVERIFY(finished.isValid());
        QImage background(200, 160, QImage::Format_ARGB32_Premultiplied); background.fill(Qt::white);
        QImage tile(100, 100, QImage::Format_ARGB32_Premultiplied); tile.fill(Qt::blue);
        auto layer = PanelMotionLayer::stationary(tile);
        layer.openPosition = QPointF(30, 40);
        layer.openClip = QSizeF(50, 60);
        auto* owner = new QObject;
        QVERIFY(h.compositor->present(owner, QRect(350, 100, 200, 160),
                                       {PanelMotionLayer::stationary(background), layer}, true, true));
        const auto image = h.compositor->grab().toImage();
        const auto pixel = [&](int x, int y) { return image.pixelColor(qRound(x * image.devicePixelRatio()), qRound(y * image.devicePixelRatio())); };
        QCOMPARE(pixel(25, 55), QColor(Qt::white));
        QCOMPARE(pixel(35, 55), QColor(Qt::blue));
        QCOMPARE(pixel(85, 55), QColor(Qt::white));
        QCOMPARE(pixel(35, 105), QColor(Qt::white));
        delete owner;
        QCOMPARE(finished.count(), 1);
        QVERIFY(!h.compositor->isActive());
        QCOMPARE(h.compositor->snapshotBytes(), 0);
        for (int scene = 0; scene < 3; ++scene) {
            auto* transient = new Harness;
            if (scene == 0) transient->drawer->restorePanel("problems");
            else {
                transient->context->setDockVisible(true);
                if (scene == 2) {
                    transient->compositor->settle();
                    transient->context->dockHost()->setSectionCollapsed(transient->keys[0], true);
                }
            }
#ifdef ZEROSLACK_ENABLE_ELA
            bool active = false;
            for (auto* motion : transient->window.findChildren<ElaDrawerArea*>()) active |= motion->isDrawerAnimating();
            QVERIFY(active);
#else
            QVERIFY(transient->compositor->isActive());
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
