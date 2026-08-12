#include "temporaryeditordrawer.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QCompleter>
#include <QContextMenuEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QSizeGrip>
#include <QSignalSpy>
#include <QTest>
#include <QTimerEvent>
#include <QToolButton>
#include <QWidget>

namespace {

void sendUnrelatedEventFlood(QWidget* target, int repetitions)
{
    for (int index = 0; index < repetitions; ++index) {
        QMouseEvent mouseMove(
            QEvent::MouseMove,
            QPointF(1.0, 1.0),
            QPointF(1.0, 1.0),
            Qt::NoButton,
            Qt::NoButton,
            Qt::NoModifier);
        QApplication::sendEvent(target, &mouseMove);

        QPaintEvent paint(QRect(0, 0, 1, 1));
        QApplication::sendEvent(target, &paint);

        QTimerEvent timer(index + 1);
        QApplication::sendEvent(target, &timer);
    }
}

} // namespace

class TemporaryEditorDrawerEventFilterTestAccess
{
public:
    static bool applicationFilterInstalled(
        const TemporaryEditorDrawer& drawer)
    {
        return drawer.applicationEventFilterInstalled;
    }

    static quint64 globalDispatchCount(
        const TemporaryEditorDrawer& drawer)
    {
        return drawer.applicationEventDispatchCount;
    }

    static void resetGlobalDispatchCount(
        TemporaryEditorDrawer& drawer)
    {
        drawer.applicationEventDispatchCount = 0;
    }

    static bool protectsPopup(
        const TemporaryEditorDrawer& drawer,
        const QWidget* popup)
    {
        return drawer.isProtectedPopup(popup);
    }

    static QWidget* titleDragSurface(TemporaryEditorDrawer& drawer)
    {
        return drawer.titleBar;
    }

    static QWidget* resizeGrip(TemporaryEditorDrawer& drawer)
    {
        return drawer.sizeGrip;
    }

    static bool dragging(const TemporaryEditorDrawer& drawer)
    {
        return drawer.draggingTitleBar;
    }

    static bool resizing(const TemporaryEditorDrawer& drawer)
    {
        return drawer.resizingWithGrip;
    }

    static int popupLifetimeObservationCount(
        const TemporaryEditorDrawer& drawer,
        const QWidget* popup)
    {
        return drawer.popupDestroyedConnections.contains(
                   const_cast<QWidget*>(popup))
            ? 1
            : 0;
    }

    static int localCompleterPopupCount(
        const TemporaryEditorDrawer& drawer)
    {
        return static_cast<int>(drawer.localCompleterPopups.size());
    }

    static quint64 focusEvaluationCount(
        const TemporaryEditorDrawer& drawer)
    {
        return drawer.focusOwnershipEvaluationCount;
    }

    static void resetFocusEvaluationCount(
        TemporaryEditorDrawer& drawer)
    {
        drawer.focusOwnershipEvaluationCount = 0;
    }
};

namespace {

void triggerDrawerContextMenuArm(TemporaryEditorDrawer& drawer)
{
    QToolButton* trigger = drawer.backButton();
    const QPoint local(4, 4);
    QContextMenuEvent contextEvent(
        QContextMenuEvent::Mouse,
        local,
        trigger->mapToGlobal(local));
    QApplication::sendEvent(trigger, &contextEvent);
}

void sendOrdinaryClick(QWidget* target)
{
    const QPoint local(3, 3);
    const QPoint global = target->mapToGlobal(local);
    QMouseEvent press(
        QEvent::MouseButtonPress,
        QPointF(local),
        QPointF(global),
        Qt::LeftButton,
        Qt::LeftButton,
        Qt::NoModifier);
    QApplication::sendEvent(target, &press);
    QMouseEvent release(
        QEvent::MouseButtonRelease,
        QPointF(local),
        QPointF(global),
        Qt::LeftButton,
        Qt::NoButton,
        Qt::NoModifier);
    QApplication::sendEvent(target, &release);
}

} // namespace

class TemporaryEditorDrawerEventFilterTest final : public QObject
{
    Q_OBJECT

private slots:
    void hiddenDrawerDoesNotObserveApplicationEvents();
    void collapsedHandleUsesOnlyRegionLocalResizeFilter();
    void hiddenAndHandleIgnoreUnrelatedFocusChanges();
    void ordinaryClicksRemainApplicationFilterFree();
    void dragAndResizeArmOnlyUntilExternalRelease();
    void visibleDrawerProtectsOwnedPopupAndUninstallsOnClose();
    void detachedEditorReleasesCompleterPopupObservation();
};

void TemporaryEditorDrawerEventFilterTest::
hiddenDrawerDoesNotObserveApplicationEvents()
{
    QWidget editorRegion;
    editorRegion.resize(900, 600);
    editorRegion.show();

    TemporaryEditorDrawer drawer(&editorRegion);
    QCOMPARE(drawer.state(), TemporaryEditorDrawer::State::Hidden);
    QVERIFY(!TemporaryEditorDrawerEventFilterTestAccess::
                applicationFilterInstalled(drawer));

    QWidget unrelated;
    TemporaryEditorDrawerEventFilterTestAccess::
        resetGlobalDispatchCount(drawer);
    sendUnrelatedEventFlood(&unrelated, 2000);

    QCOMPARE(TemporaryEditorDrawerEventFilterTestAccess::
                 globalDispatchCount(drawer),
             quint64(0));

    const QRect beforeResize = drawer.geometry();
    editorRegion.resize(720, 480);
    QApplication::processEvents();
    QCOMPARE(drawer.state(), TemporaryEditorDrawer::State::Hidden);
    QCOMPARE(drawer.geometry(), beforeResize);
}

void TemporaryEditorDrawerEventFilterTest::
collapsedHandleUsesOnlyRegionLocalResizeFilter()
{
    QWidget editorRegion;
    editorRegion.resize(900, 600);
    editorRegion.show();

    TemporaryEditorDrawer drawer(&editorRegion);
    EditorLocation location;
    location.filePath = QStringLiteral("C:/workspace/local_resize.sv");
    location.line = 7;
    drawer.open(location);
    drawer.stow(TemporaryEditorDrawer::Edge::Right);

    QCOMPARE(drawer.state(), TemporaryEditorDrawer::State::EdgeStowed);
    QVERIFY(drawer.isHandleVisible());
    QVERIFY(!TemporaryEditorDrawerEventFilterTestAccess::
                applicationFilterInstalled(drawer));

    editorRegion.resize(720, 480);
    QApplication::processEvents();
    QCOMPARE(drawer.handleRect().right(), editorRegion.rect().right());
    QVERIFY(!TemporaryEditorDrawerEventFilterTestAccess::
                applicationFilterInstalled(drawer));
}

void TemporaryEditorDrawerEventFilterTest::
hiddenAndHandleIgnoreUnrelatedFocusChanges()
{
    QWidget editorRegion;
    editorRegion.resize(900, 600);
    editorRegion.show();
    TemporaryEditorDrawer drawer(&editorRegion);

    QWidget unrelatedWindow;
    unrelatedWindow.resize(240, 120);
    QLineEdit first(&unrelatedWindow);
    QLineEdit second(&unrelatedWindow);
    first.setGeometry(10, 10, 180, 28);
    second.setGeometry(10, 50, 180, 28);
    unrelatedWindow.show();
    unrelatedWindow.activateWindow();

    auto exerciseUnrelatedFocus = [&]() {
        for (int index = 0; index < 20; ++index) {
            (index % 2 == 0 ? &first : &second)
                ->setFocus(Qt::OtherFocusReason);
            QApplication::processEvents();
        }
    };

    TemporaryEditorDrawerEventFilterTestAccess::
        resetFocusEvaluationCount(drawer);
    QSignalSpy focusSpy(qApp, &QApplication::focusChanged);
    exerciseUnrelatedFocus();
    QVERIFY(focusSpy.count() > 0);
    QCOMPARE(TemporaryEditorDrawerEventFilterTestAccess::
                 focusEvaluationCount(drawer),
             quint64(0));

    EditorLocation location;
    location.filePath = QStringLiteral("C:/workspace/focus_guard.sv");
    drawer.open(location);
    drawer.stow(TemporaryEditorDrawer::Edge::Right);
    TemporaryEditorDrawerEventFilterTestAccess::
        resetFocusEvaluationCount(drawer);
    focusSpy.clear();
    exerciseUnrelatedFocus();
    QVERIFY(focusSpy.count() > 0);
    QCOMPARE(TemporaryEditorDrawerEventFilterTestAccess::
                 focusEvaluationCount(drawer),
             quint64(0));
    QVERIFY(!TemporaryEditorDrawerEventFilterTestAccess::
                 applicationFilterInstalled(drawer));
}

void TemporaryEditorDrawerEventFilterTest::
ordinaryClicksRemainApplicationFilterFree()
{
    QWidget editorRegion;
    editorRegion.resize(900, 600);
    editorRegion.show();

    TemporaryEditorDrawer drawer(&editorRegion);
    EditorLocation location;
    location.filePath = QStringLiteral("C:/workspace/ordinary_click.sv");
    drawer.open(location);
    auto* editorChild = new QWidget;
    editorChild->setMinimumSize(120, 80);
    drawer.setEditorWidget(editorChild);
    QApplication::processEvents();

    TemporaryEditorDrawerEventFilterTestAccess::
        resetGlobalDispatchCount(drawer);
    sendOrdinaryClick(editorChild);
    QVERIFY(!TemporaryEditorDrawerEventFilterTestAccess::
                 applicationFilterInstalled(drawer));
    sendOrdinaryClick(drawer.pinButton());
    QVERIFY(!TemporaryEditorDrawerEventFilterTestAccess::
                 applicationFilterInstalled(drawer));
    sendOrdinaryClick(drawer.backButton());
    QVERIFY(!TemporaryEditorDrawerEventFilterTestAccess::
                 applicationFilterInstalled(drawer));

    QWidget unrelated;
    sendUnrelatedEventFlood(&unrelated, 1000);
    QCOMPARE(TemporaryEditorDrawerEventFilterTestAccess::
                 globalDispatchCount(drawer),
             quint64(0));
}

void TemporaryEditorDrawerEventFilterTest::
dragAndResizeArmOnlyUntilExternalRelease()
{
    QWidget editorRegion;
    editorRegion.resize(900, 600);
    editorRegion.show();

    TemporaryEditorDrawer drawer(&editorRegion);
    EditorLocation location;
    location.filePath = QStringLiteral("C:/workspace/drag_release.sv");
    drawer.open(location);
    QVERIFY(!TemporaryEditorDrawerEventFilterTestAccess::
                 applicationFilterInstalled(drawer));

    QWidget unrelated;
    unrelated.show();
    const QPoint globalCenter =
        editorRegion.mapToGlobal(editorRegion.rect().center());

    QWidget* title = TemporaryEditorDrawerEventFilterTestAccess::
        titleDragSurface(drawer);
    QMouseEvent titlePress(
        QEvent::MouseButtonPress,
        QPointF(3.0, 3.0),
        QPointF(title->mapToGlobal(QPoint(3, 3))),
        Qt::LeftButton,
        Qt::LeftButton,
        Qt::NoModifier);
    QApplication::sendEvent(title, &titlePress);
    QVERIFY(TemporaryEditorDrawerEventFilterTestAccess::dragging(drawer));
    QVERIFY(TemporaryEditorDrawerEventFilterTestAccess::
                applicationFilterInstalled(drawer));

    QMouseEvent titleRelease(
        QEvent::MouseButtonRelease,
        QPointF(1.0, 1.0),
        QPointF(globalCenter),
        Qt::LeftButton,
        Qt::NoButton,
        Qt::NoModifier);
    QApplication::sendEvent(&unrelated, &titleRelease);
    QVERIFY(!TemporaryEditorDrawerEventFilterTestAccess::dragging(drawer));
    QVERIFY(!TemporaryEditorDrawerEventFilterTestAccess::
                 applicationFilterInstalled(drawer));

    QWidget* grip = TemporaryEditorDrawerEventFilterTestAccess::
        resizeGrip(drawer);
    QMouseEvent gripPress(
        QEvent::MouseButtonPress,
        QPointF(2.0, 2.0),
        QPointF(grip->mapToGlobal(QPoint(2, 2))),
        Qt::LeftButton,
        Qt::LeftButton,
        Qt::NoModifier);
    QApplication::sendEvent(grip, &gripPress);
    QVERIFY(TemporaryEditorDrawerEventFilterTestAccess::resizing(drawer));
    QVERIFY(TemporaryEditorDrawerEventFilterTestAccess::
                applicationFilterInstalled(drawer));

    QMouseEvent gripRelease(
        QEvent::MouseButtonRelease,
        QPointF(1.0, 1.0),
        QPointF(globalCenter),
        Qt::LeftButton,
        Qt::NoButton,
        Qt::NoModifier);
    QApplication::sendEvent(&unrelated, &gripRelease);
    QVERIFY(!TemporaryEditorDrawerEventFilterTestAccess::resizing(drawer));
    QVERIFY(!TemporaryEditorDrawerEventFilterTestAccess::
                 applicationFilterInstalled(drawer));
}

void TemporaryEditorDrawerEventFilterTest::
visibleDrawerProtectsOwnedPopupAndUninstallsOnClose()
{
    QWidget editorRegion;
    editorRegion.resize(900, 600);
    editorRegion.show();

    TemporaryEditorDrawer drawer(&editorRegion);
    EditorLocation location;
    location.filePath = QStringLiteral("C:/workspace/owned_popup.sv");
    location.line = 4;
    drawer.open(location);
    QVERIFY(!TemporaryEditorDrawerEventFilterTestAccess::
                 applicationFilterInstalled(drawer));

    QWidget unrelated;
    TemporaryEditorDrawerEventFilterTestAccess::
        resetGlobalDispatchCount(drawer);
    sendUnrelatedEventFlood(&unrelated, 2000);
    QCOMPARE(TemporaryEditorDrawerEventFilterTestAccess::
                 globalDispatchCount(drawer),
             quint64(0));

    drawer.searchField()->setFocus(Qt::PopupFocusReason);
    QEvent drawerEnter(QEvent::Enter);
    QApplication::sendEvent(&drawer, &drawerEnter);

    QWidget otherPanel;
    otherPanel.resize(240, 160);
    otherPanel.show();
    triggerDrawerContextMenuArm(drawer);
    QVERIFY(TemporaryEditorDrawerEventFilterTestAccess::
                applicationFilterInstalled(drawer));
    QMenu unrelatedMenu(&otherPanel);
    unrelatedMenu.addAction(QStringLiteral("Unrelated action"));
    unrelatedMenu.popup(otherPanel.mapToGlobal(QPoint(20, 20)));
    QApplication::processEvents();
    QVERIFY(!TemporaryEditorDrawerEventFilterTestAccess::
                 protectsPopup(drawer, &unrelatedMenu));
    unrelatedMenu.hide();
    QTRY_VERIFY_WITH_TIMEOUT(
        !TemporaryEditorDrawerEventFilterTestAccess::
             applicationFilterInstalled(drawer),
        1500);

    triggerDrawerContextMenuArm(drawer);
    QVERIFY(TemporaryEditorDrawerEventFilterTestAccess::
                applicationFilterInstalled(drawer));
    QMenu ownedMenu(&drawer);
    ownedMenu.addAction(QStringLiteral("Owned action"));
    ownedMenu.popup(editorRegion.mapToGlobal(QPoint(40, 40)));
    QApplication::processEvents();
    QVERIFY(TemporaryEditorDrawerEventFilterTestAccess::
                protectsPopup(drawer, &ownedMenu));
    QCOMPARE(TemporaryEditorDrawerEventFilterTestAccess::
                 popupLifetimeObservationCount(drawer, &ownedMenu),
             1);
    QVERIFY(TemporaryEditorDrawerEventFilterTestAccess::
                globalDispatchCount(drawer) > quint64(0));

    ownedMenu.hide();
    QApplication::processEvents();
    QVERIFY(!TemporaryEditorDrawerEventFilterTestAccess::
                 protectsPopup(drawer, &ownedMenu));
    QTRY_VERIFY_WITH_TIMEOUT(
        !TemporaryEditorDrawerEventFilterTestAccess::
             applicationFilterInstalled(drawer),
        500);

    triggerDrawerContextMenuArm(drawer);
    ownedMenu.popup(editorRegion.mapToGlobal(QPoint(42, 42)));
    QApplication::processEvents();
    QVERIFY(TemporaryEditorDrawerEventFilterTestAccess::
                protectsPopup(drawer, &ownedMenu));
    QCOMPARE(TemporaryEditorDrawerEventFilterTestAccess::
                 popupLifetimeObservationCount(drawer, &ownedMenu),
             1);
    ownedMenu.hide();
    QTRY_VERIFY_WITH_TIMEOUT(
        !TemporaryEditorDrawerEventFilterTestAccess::
             applicationFilterInstalled(drawer),
        500);

    QCompleter completion(
        QStringList{QStringLiteral("alpha.sv"),
                    QStringLiteral("alpine.sv")},
        &drawer);
    drawer.searchField()->setCompleter(&completion);
    drawer.searchField()->setFocus(Qt::PopupFocusReason);
    for (int index = 0; index < 20; ++index) {
        drawer.searchField()->setText(
            QStringLiteral("idle_%1").arg(index));
        QKeyEvent ordinaryInput(
            QEvent::KeyPress,
            Qt::Key_Left,
            Qt::NoModifier);
        QApplication::sendEvent(drawer.searchField(), &ordinaryInput);
    }
    QVERIFY(!TemporaryEditorDrawerEventFilterTestAccess::
                 applicationFilterInstalled(drawer));
    drawer.searchField()->setText(QStringLiteral("al"));
    QVERIFY(!TemporaryEditorDrawerEventFilterTestAccess::
                 applicationFilterInstalled(drawer));
    completion.setCompletionPrefix(QStringLiteral("al"));
    completion.complete();
    QTRY_VERIFY_WITH_TIMEOUT(completion.popup()->isVisible(), 500);
    QVERIFY(TemporaryEditorDrawerEventFilterTestAccess::
                applicationFilterInstalled(drawer));
    QVERIFY(TemporaryEditorDrawerEventFilterTestAccess::
                protectsPopup(drawer, completion.popup()));
    completion.popup()->hide();
    QApplication::processEvents();
    QVERIFY(!TemporaryEditorDrawerEventFilterTestAccess::
                 protectsPopup(drawer, completion.popup()));
    QTRY_VERIFY_WITH_TIMEOUT(
        !TemporaryEditorDrawerEventFilterTestAccess::
             applicationFilterInstalled(drawer),
        500);

    drawer.closeDrawer();
    QVERIFY(!TemporaryEditorDrawerEventFilterTestAccess::
                applicationFilterInstalled(drawer));

    TemporaryEditorDrawerEventFilterTestAccess::
        resetGlobalDispatchCount(drawer);
    sendUnrelatedEventFlood(&unrelated, 2000);
    QCOMPARE(TemporaryEditorDrawerEventFilterTestAccess::
                 globalDispatchCount(drawer),
             quint64(0));
}

void TemporaryEditorDrawerEventFilterTest::
detachedEditorReleasesCompleterPopupObservation()
{
    QWidget editorRegion;
    editorRegion.resize(900, 600);
    editorRegion.show();

    TemporaryEditorDrawer drawer(&editorRegion);
    EditorLocation location;
    location.filePath = QStringLiteral("C:/workspace/detach_popup.sv");
    drawer.open(location);

    QWidget auxiliaryEditor;
    auto* completionField = new QLineEdit(&auxiliaryEditor);
    completionField->resize(240, 30);
    auto* completion = new QCompleter(
        QStringList{QStringLiteral("alpha"),
                    QStringLiteral("alpine")},
        &auxiliaryEditor);
    completionField->setCompleter(completion);
    drawer.setEditorWidget(&auxiliaryEditor);
    completionField->show();
    completionField->setFocus(Qt::PopupFocusReason);
    completionField->setText(QStringLiteral("al"));
    completion->setCompletionPrefix(QStringLiteral("al"));
    completion->complete();
    QTRY_VERIFY_WITH_TIMEOUT(completion->popup()->isVisible(), 500);
    QVERIFY(TemporaryEditorDrawerEventFilterTestAccess::
                applicationFilterInstalled(drawer));
    QVERIFY(TemporaryEditorDrawerEventFilterTestAccess::
                protectsPopup(drawer, completion->popup()));
    QCOMPARE(TemporaryEditorDrawerEventFilterTestAccess::
                 popupLifetimeObservationCount(
                     drawer, completion->popup()),
             1);
    QCOMPARE(TemporaryEditorDrawerEventFilterTestAccess::
                 localCompleterPopupCount(drawer),
             1);

    QCOMPARE(drawer.takeEditorWidget(), &auxiliaryEditor);
    QVERIFY(!TemporaryEditorDrawerEventFilterTestAccess::
                 applicationFilterInstalled(drawer));
    QVERIFY(!TemporaryEditorDrawerEventFilterTestAccess::
                 protectsPopup(drawer, completion->popup()));
    QCOMPARE(TemporaryEditorDrawerEventFilterTestAccess::
                 popupLifetimeObservationCount(
                     drawer, completion->popup()),
             0);
    QCOMPARE(TemporaryEditorDrawerEventFilterTestAccess::
                 localCompleterPopupCount(drawer),
             0);

    completion->complete();
    QApplication::processEvents();
    QVERIFY(!TemporaryEditorDrawerEventFilterTestAccess::
                 protectsPopup(drawer, completion->popup()));
    QVERIFY(!TemporaryEditorDrawerEventFilterTestAccess::
                 applicationFilterInstalled(drawer));
}

QTEST_MAIN(TemporaryEditorDrawerEventFilterTest)
#include "temporary_editor_drawer_event_filter_test.moc"
