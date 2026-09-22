#include "applicationthememanager.h"
#include "contextdockhost.h"
#include "contextworkspacecontroller.h"
#include "navigationpanecoordinator.h"
#include "panelcompositor.h"
#include "panellayoutcontroller.h"
#include "testuistyle.h"
#include <QApplication>
#include <QDockWidget>
#include <QLabel>
#include <QLayout>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QScrollArea>
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
    void drawerReversesWithoutMovingButtonBar() {
        Harness h;
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

    void panelsShareOnePresenterAndPreemptSafely() {
        Harness h;
        h.navigation->setExpanded(false);
        QVERIFY(h.navigation->isAnimating());
        h.context->setDockVisible(true);
        QVERIFY(!h.navigation->isAnimating());
        QVERIFY(h.compositor->isActiveFor(h.context->dockWidget()));
        h.drawer->restorePanel("problems");
        QVERIFY(h.compositor->isActiveFor(h.drawer));
        h.context->dockHost()->setSectionCollapsed(h.keys[0], true);
        QVERIFY(h.compositor->isActiveFor(h.context->dockHost()->sectionWidget(h.keys[0])));
        QCOMPARE(h.window.findChildren<PanelCompositor*>().size(), 1);
        h.window.resize(1250, 810);
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
            QVERIFY(transient->compositor->isActive());
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
