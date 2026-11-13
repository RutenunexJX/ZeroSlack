#include "actionregistry.h"
#include "editordroppreviewoverlay.h"
#include "editorsplitcontroller.h"
#include "uicontrols.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QJsonArray>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPlainTextDocumentLayout>
#include <QPixmap>
#include <QScrollBar>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QTextCursor>
#include <QTextBlock>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <iostream>

namespace {
constexpr const char* kEditorTabMime =
    "application/x-zeroslack-editor-tab";

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

QWidget* page(const QString& id, const QString& text)
{
    auto* value = new QLabel(text);
    value->setProperty("editorViewId", id);
    return value;
}

void setEditorMime(QMimeData* mime, const QString& viewId)
{
    mime->setData(
        QString::fromLatin1(kEditorTabMime),
        viewId.toUtf8());
}

bool sendDragEnter(
    QWidget* target,
    const QString& viewId,
    const QPoint& position)
{
    QMimeData mime;
    setEditorMime(&mime, viewId);
    QDragEnterEvent event(
        position,
        Qt::MoveAction,
        &mime,
        Qt::LeftButton,
        Qt::NoModifier);
    event.ignore();
    QApplication::sendEvent(target, &event);
    return event.isAccepted();
}

bool sendDragMove(
    QWidget* target,
    const QString& viewId,
    const QPoint& position)
{
    QMimeData mime;
    setEditorMime(&mime, viewId);
    QDragMoveEvent event(
        position,
        Qt::MoveAction,
        &mime,
        Qt::LeftButton,
        Qt::NoModifier);
    event.ignore();
    QApplication::sendEvent(target, &event);
    return event.isAccepted();
}

bool sendDrop(
    QWidget* target,
    const QString& viewId,
    const QPoint& position)
{
    QMimeData mime;
    setEditorMime(&mime, viewId);
    QDropEvent event(
        QPointF(position),
        Qt::MoveAction,
        &mime,
        Qt::LeftButton,
        Qt::NoModifier);
    event.ignore();
    QApplication::sendEvent(target, &event);
    return event.isAccepted();
}

EditorDropPreviewOverlay* previewOverlay(QWidget* host)
{
    QWidget* widget = host
        ? host->findChild<QWidget*>(
              QStringLiteral("editorDropPreviewOverlay"))
        : nullptr;
    return dynamic_cast<EditorDropPreviewOverlay*>(widget);
}

QPoint samplePoint(
    const QRect& rect,
    EditorSplitDirection direction)
{
    switch (direction) {
    case EditorSplitDirection::Left:
        return QPoint(rect.left() + 2, rect.center().y());
    case EditorSplitDirection::Right:
        return QPoint(rect.right() - 2, rect.center().y());
    case EditorSplitDirection::Above:
        return QPoint(rect.center().x(), rect.top() + 2);
    case EditorSplitDirection::Below:
        return QPoint(rect.center().x(), rect.bottom() - 2);
    case EditorSplitDirection::Center:
        return rect.center();
    }
    return rect.center();
}

void testPreviewGeometryAndDragVisual()
{
    const QRect rect(0, 0, 801, 501);
    const EditorSplitDirection directions[] = {
        EditorSplitDirection::Left,
        EditorSplitDirection::Right,
        EditorSplitDirection::Above,
        EditorSplitDirection::Below,
        EditorSplitDirection::Center,
    };
    bool fiveHitZonesMatch = true;
    bool fivePreviewRectsMatch = true;
    for (EditorSplitDirection direction : directions) {
        const QPoint point = samplePoint(rect, direction);
        fiveHitZonesMatch = fiveHitZonesMatch
            && EditorDropPreviewOverlay::directionAt(
                   rect, point)
                == direction;
        const QRect preview =
            EditorDropPreviewOverlay::previewRect(
                rect, direction);
        if (direction == EditorSplitDirection::Center) {
            fivePreviewRectsMatch = fivePreviewRectsMatch
                && preview == rect;
        } else if (direction == EditorSplitDirection::Left
                   || direction == EditorSplitDirection::Right) {
            fivePreviewRectsMatch = fivePreviewRectsMatch
                && preview.height() == rect.height()
                && preview.width() > 0
                && preview.width() <= (rect.width() + 1) / 2;
        } else {
            fivePreviewRectsMatch = fivePreviewRectsMatch
                && preview.width() == rect.width()
                && preview.height() > 0
                && preview.height() <= (rect.height() + 1) / 2;
        }
    }
    check(fiveHitZonesMatch,
          "drop preview exposes all five destination hit zones");
    check(fivePreviewRectsMatch,
          "drop preview paints matching half or full destination geometry");

    QTabWidget tabs;
    tabs.addTab(
        new QWidget(&tabs),
        QStringLiteral("current_view_with_a_distinct_label.sv"));
    tabs.resize(440, 260);
    tabs.show();
    QApplication::processEvents();
    const QPixmap dragPixmap =
        EditorDropPreviewOverlay::tabDragPixmap(
            tabs.tabBar(), 0);
    check(!dragPixmap.isNull()
              && dragPixmap.width() > 40
              && dragPixmap.height() > 20,
          "cross-group tab drag has a visible label pixmap");
}

void testPreviewLifecycle()
{
    QWidget host;
    auto* layout = new QHBoxLayout(&host);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* initial = new QTabWidget(&host);
    layout->addWidget(initial);
    QWidget* source = page(
        QStringLiteral("preview-source"),
        QStringLiteral("source"));
    initial->addTab(source, QStringLiteral("source.sv"));
    initial->addTab(
        page(QStringLiteral("preview-keeper"),
             QStringLiteral("keeper")),
        QStringLiteral("keeper.sv"));

    EditorSplitController controller(initial);
    controller.setHost(&host);
    QTabWidget* target = controller.createSplit(
        initial, EditorSplitDirection::Right);
    target->addTab(
        page(QStringLiteral("preview-target"),
             QStringLiteral("target")),
        QStringLiteral("target.sv"));
    host.resize(960, 600);
    host.show();
    QApplication::processEvents();

    const QString sourceId = QStringLiteral("preview-source");
    const QPoint leftPoint = samplePoint(
        target->rect(), EditorSplitDirection::Left);
    check(sendDragEnter(target, sourceId, leftPoint),
          "editor tab drag enter is accepted for a live View id");
    QApplication::processEvents();
    EditorDropPreviewOverlay* overlay = previewOverlay(&host);
    const QRect expectedGeometry(
        target->mapTo(&host, QPoint(0, 0)),
        target->size());
    check(overlay
              && overlay->isVisible()
              && overlay->target() == target
              && overlay->geometry() == expectedGeometry
              && overlay->testAttribute(
                  Qt::WA_TransparentForMouseEvents),
          "preview overlays the target without intercepting pointer events");

    const EditorSplitDirection directions[] = {
        EditorSplitDirection::Left,
        EditorSplitDirection::Right,
        EditorSplitDirection::Above,
        EditorSplitDirection::Below,
        EditorSplitDirection::Center,
    };
    bool liveDirectionsMatch = true;
    const QRect targetContentRect(
        overlay->mapFromGlobal(
            target->currentWidget()->mapToGlobal(QPoint(0, 0))),
        target->currentWidget()->size());
    for (EditorSplitDirection direction : directions) {
        liveDirectionsMatch = liveDirectionsMatch
            && sendDragMove(
                target,
                sourceId,
                samplePoint(target->rect(), direction));
        QApplication::processEvents();
        liveDirectionsMatch = liveDirectionsMatch
            && overlay->direction() == direction
            && overlay->highlightedRect()
                == EditorDropPreviewOverlay::previewRect(
                    targetContentRect, direction);
    }
    check(liveDirectionsMatch,
          "drag move updates the live overlay across all five zones");

    const bool targetSwitchAccepted = sendDragEnter(
        initial,
        sourceId,
        samplePoint(
            initial->rect(),
            EditorSplitDirection::Center));
    const bool targetSwitchMatches =
        overlay->target() == initial;
    check(targetSwitchAccepted && targetSwitchMatches,
          "switching targets immediately reuses the overlay on the new group");

    QDragLeaveEvent leave;
    QApplication::sendEvent(initial, &leave);
    check(!overlay->isVisible() && !overlay->target(),
          "drag leave clears the preview without residue");

    sendDragEnter(target, sourceId, leftPoint);
    QKeyEvent escape(
        QEvent::KeyPress,
        Qt::Key_Escape,
        Qt::NoModifier);
    QApplication::sendEvent(target, &escape);
    check(!overlay->isVisible(),
          "Escape cancellation clears the active preview");

    sendDragEnter(target, sourceId, leftPoint);
    delete source;
    QApplication::processEvents();
    check(!overlay->isVisible(),
          "destroying the dragged source View clears the preview");

    const QString keeperId = QStringLiteral("preview-keeper");
    sendDragEnter(target, keeperId, leftPoint);
    target->hide();
    QApplication::processEvents();
    check(!overlay->isVisible(),
          "hiding or destroying a preview target cannot leave an overlay");
}

void testDropResultMatchesPreview()
{
    const EditorSplitDirection directions[] = {
        EditorSplitDirection::Left,
        EditorSplitDirection::Right,
        EditorSplitDirection::Above,
        EditorSplitDirection::Below,
        EditorSplitDirection::Center,
    };

    bool allDropsMatch = true;
    for (int caseIndex = 0; caseIndex < 5; ++caseIndex) {
        const EditorSplitDirection direction = directions[caseIndex];
        QWidget host;
        auto* layout = new QHBoxLayout(&host);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* initial = new QTabWidget(&host);
        layout->addWidget(initial);
        const QString sourceId = QStringLiteral("drop-source-%1")
                                     .arg(caseIndex);
        QWidget* source = page(sourceId, QStringLiteral("source"));
        initial->addTab(source, QStringLiteral("source.sv"));
        initial->addTab(
            page(QStringLiteral("drop-keeper-%1").arg(caseIndex),
                 QStringLiteral("keeper")),
            QStringLiteral("keeper.sv"));

        EditorSplitController controller(initial);
        controller.setHost(&host);
        QTabWidget* target = controller.createSplit(
            initial, EditorSplitDirection::Right);
        target->addTab(
            page(QStringLiteral("drop-target-%1").arg(caseIndex),
                 QStringLiteral("target")),
            QStringLiteral("target.sv"));
        host.resize(920, 560);
        host.show();
        QApplication::processEvents();

        const QPoint point = samplePoint(target->rect(), direction);
        allDropsMatch = allDropsMatch
            && sendDragEnter(target, sourceId, point);
        EditorDropPreviewOverlay* overlay = previewOverlay(&host);
        allDropsMatch = allDropsMatch
            && overlay
            && overlay->isVisible()
            && overlay->direction() == direction;
        allDropsMatch = allDropsMatch
            && sendDrop(target, sourceId, point);
        QApplication::processEvents();
        allDropsMatch = allDropsMatch
            && overlay
            && !overlay->isVisible();

        QTabWidget* destination = controller.groupForPage(source);
        if (direction == EditorSplitDirection::Center) {
            allDropsMatch = allDropsMatch
                && destination == target;
            continue;
        }

        QSplitter* splitter = destination
            ? qobject_cast<QSplitter*>(
                  destination->parentWidget())
            : nullptr;
        const Qt::Orientation expectedOrientation =
            direction == EditorSplitDirection::Left
                    || direction == EditorSplitDirection::Right
                ? Qt::Horizontal
                : Qt::Vertical;
        const bool before =
            direction == EditorSplitDirection::Left
            || direction == EditorSplitDirection::Above;
        allDropsMatch = allDropsMatch
            && destination
            && destination != target
            && splitter
            && target->parentWidget() == splitter
            && splitter->orientation() == expectedOrientation
            && (before
                    ? splitter->indexOf(destination)
                          < splitter->indexOf(target)
                    : splitter->indexOf(destination)
                          > splitter->indexOf(target));
    }
    check(allDropsMatch,
          "real drops create the same center or directional layout shown by the preview");
}

void testNestedSplitterDoesNotMutateBeforeDrop()
{
    QWidget host;
    auto* layout = new QHBoxLayout(&host);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* initial = new QTabWidget(&host);
    layout->addWidget(initial);
    initial->addTab(
        page(QStringLiteral("nested-keeper"),
             QStringLiteral("keeper")),
        QStringLiteral("keeper.sv"));

    EditorSplitController controller(initial);
    controller.setHost(&host);
    QTabWidget* right = controller.createSplit(
        initial, EditorSplitDirection::Right);
    QWidget* source = page(
        QStringLiteral("nested-source"),
        QStringLiteral("source"));
    right->addTab(source, QStringLiteral("source.sv"));
    right->addTab(
        page(QStringLiteral("nested-right-keeper"),
             QStringLiteral("right keeper")),
        QStringLiteral("right_keeper.sv"));
    QTabWidget* below = controller.createSplit(
        initial, EditorSplitDirection::Below);
    below->addTab(
        page(QStringLiteral("nested-target"),
             QStringLiteral("target")),
        QStringLiteral("target.sv"));
    host.resize(1000, 640);
    host.show();
    QApplication::processEvents();

    const int splittersBefore =
        host.findChildren<QSplitter*>().size();
    const QPoint leftPoint = samplePoint(
        below->rect(), EditorSplitDirection::Left);
    const bool previewAccepted = sendDragEnter(
        below,
        QStringLiteral("nested-source"),
        leftPoint);
    QApplication::processEvents();
    const bool unchangedDuringPreview =
        host.findChildren<QSplitter*>().size()
            == splittersBefore
        && controller.groupForPage(source) == right;
    const bool dropAccepted = sendDrop(
        below,
        QStringLiteral("nested-source"),
        leftPoint);
    QApplication::processEvents();
    QTabWidget* destination = controller.groupForPage(source);
    QSplitter* destinationParent = destination
        ? qobject_cast<QSplitter*>(destination->parentWidget())
        : nullptr;
    check(previewAccepted
              && unchangedDuringPreview
              && dropAccepted
              && destination != right
              && destinationParent
              && destinationParent->orientation() == Qt::Horizontal
              && destinationParent->indexOf(destination)
                  < destinationParent->indexOf(below),
          "nested splitter preview is non-mutating and drop applies its direction");
}

void testSharedDocumentViewsKeepIndependentState()
{
    QWidget host;
    auto* layout = new QVBoxLayout(&host);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* initial = new QTabWidget(&host);
    layout->addWidget(initial);
    auto* document = new QTextDocument(&host);
    document->setDocumentLayout(
        new QPlainTextDocumentLayout(document));
    QStringList lines;
    for (int index = 0; index < 240; ++index) {
        lines.append(
            QStringLiteral("line_%1_%2")
                .arg(index)
                .arg(QString(260, QLatin1Char('x'))));
    }
    document->setPlainText(lines.join(QLatin1Char('\n')));

    auto* firstView = new QPlainTextEdit;
    firstView->setDocument(document);
    firstView->setLineWrapMode(QPlainTextEdit::NoWrap);
    firstView->setProperty(
        "editorViewId", QStringLiteral("shared-view-one"));
    firstView->setProperty(
        "editorFileIdentity", QStringLiteral("shared.sv"));
    auto* secondView = new QPlainTextEdit;
    secondView->setDocument(document);
    secondView->setLineWrapMode(QPlainTextEdit::NoWrap);
    secondView->setProperty(
        "editorViewId", QStringLiteral("shared-view-two"));
    secondView->setProperty(
        "editorFileIdentity", QStringLiteral("shared.sv"));
    const bool documentSharedBeforeTabs =
        firstView->document() == document
        && secondView->document() == document;
    initial->addTab(firstView, QStringLiteral("shared.sv: View 1"));
    initial->addTab(
        page(QStringLiteral("shared-keeper"),
             QStringLiteral("keeper")),
        QStringLiteral("keeper.sv"));

    EditorSplitController controller(initial);
    controller.setHost(&host);
    QTabWidget* target = controller.createSplit(
        initial, EditorSplitDirection::Right);
    target->addTab(secondView, QStringLiteral("shared.sv: View 2"));
    host.resize(1080, 660);
    host.show();
    QApplication::processEvents();

    QTextCursor firstCursor(
        document->findBlockByNumber(40));
    firstCursor.setPosition(
        firstCursor.block().position() + 160);
    firstView->setTextCursor(firstCursor);
    QTextCursor secondCursor(
        document->findBlockByNumber(130));
    secondCursor.setPosition(
        secondCursor.block().position() + 220);
    secondView->setTextCursor(secondCursor);
    firstView->verticalScrollBar()->setValue(35);
    firstView->horizontalScrollBar()->setValue(800);
    secondView->verticalScrollBar()->setValue(122);
    secondView->horizontalScrollBar()->setValue(1200);
    const int firstCursorPosition =
        firstView->textCursor().position();
    const int secondCursorPosition =
        secondView->textCursor().position();
    const int firstVertical =
        firstView->verticalScrollBar()->value();
    const int firstHorizontal =
        firstView->horizontalScrollBar()->value();
    const int secondVertical =
        secondView->verticalScrollBar()->value();
    const int secondHorizontal =
        secondView->horizontalScrollBar()->value();

    QWidget* editorDropSurface = secondView->viewport();
    const QPoint rightPoint = samplePoint(
        editorDropSurface->rect(), EditorSplitDirection::Right);
    const bool entered = sendDragEnter(
        editorDropSurface,
        QStringLiteral("shared-view-one"),
        rightPoint);
    const bool dropped = sendDrop(
        editorDropSurface,
        QStringLiteral("shared-view-one"),
        rightPoint);
    QApplication::processEvents();

    const bool firstViewMoved =
        controller.groupForPage(firstView) != target;
    const bool secondViewStayed =
        controller.groupForPage(secondView) == target;
    const bool documentStayedShared =
        firstView->document() == document
        && secondView->document() == document;
    check(entered
              && dropped
              && firstViewMoved
              && secondViewStayed
              && documentSharedBeforeTabs
              && documentStayedShared,
          "unique editorViewId moves only the selected View of a shared file");
    check(firstView->textCursor().position()
                  == firstCursorPosition
              && secondView->textCursor().position()
                  == secondCursorPosition
              && firstView->verticalScrollBar()->value()
                  == firstVertical
              && firstView->horizontalScrollBar()->value()
                  == firstHorizontal
              && secondView->verticalScrollBar()->value()
                  == secondVertical
              && secondView->horizontalScrollBar()->value()
                  == secondHorizontal,
          "shared-document Views retain independent cursor and scroll state after drop");
}
}

void testUnifiedRegions()
{
    QWidget window;
    auto* layout=new QHBoxLayout(&window);
    auto* mainHost=new QWidget(&window); layout->addWidget(mainHost);
    auto* first=UiControls::editorTabWidget(mainHost);
    EditorSplitController registry(first);
    registry.setHost(mainHost);
    QList<QTabWidget*> anchors{first};
    for (const auto& id : {QStringLiteral("left"),QStringLiteral("right"),QStringLiteral("bottom")}) {
        auto* host=new QWidget(&window); layout->addWidget(host);
        anchors.append(registry.registerRegion(id,host));
    }
    auto* editor=new QPlainTextEdit;
    editor->setPlainText(QStringLiteral("alpha\nbeta\ngamma"));
    first->addTab(editor,QStringLiteral("code.sv"));
    registry.registerPage(editor,QStringLiteral("test:code"),QStringLiteral("code"));
    auto* tool=new QLabel(QStringLiteral("background data"));
    first->addTab(tool,QStringLiteral("Tool"));
    check(registry.registerPage(tool,QStringLiteral("test:tool"),QStringLiteral("tool"),[&] { return registry.parkPage(tool); }),
          "generic pages register without editorViewId");
    const auto identity=registry.pageId(editor);
    window.resize(1200,700); window.show(); QApplication::processEvents();
    registry.activatePage(editor);
    auto cursor=editor->textCursor(); cursor.setPosition(3); editor->setTextCursor(cursor); editor->insertPlainText(QStringLiteral("!"));
    const int position=editor->textCursor().position();
    bool atomic=true; int moves=0;
    QObject::connect(&registry,&EditorSplitController::pageMoved,&window,[&](QWidget* page,QTabWidget* source,QTabWidget* target) {
        ++moves; atomic &= source->indexOf(page)<0 && target->indexOf(page)>=0;
    });
    for (auto* group : anchors) {
        check(registry.movePage(editor,group),"page moves through registered regions");
        check(registry.pageId(editor)==identity && editor->textCursor().position()==position
              && registry.groupForPage(editor)==group,"movement retains page identity and cursor");
    }
    check(atomic && moves==3,"cross-region layout is published only after ownership commits");
    editor->undo(); check(editor->toPlainText()==QStringLiteral("alpha\nbeta\ngamma"),"move preserves undo history");
    editor->redo(); check(editor->toPlainText().contains(QStringLiteral("alp!ha")),"move preserves redo history");
    QTabWidget foreign;
    const auto originalGroup=registry.groupForPage(editor);
    const int originalIndex=originalGroup->indexOf(editor);
    check(!registry.movePage(editor,&foreign) && registry.groupForPage(editor)==originalGroup
          && originalGroup->indexOf(editor)==originalIndex,"invalid destination leaves original page and order intact");
    check(registry.movePage(tool,anchors[1]) && registry.requestPageClose(tool)
          && !registry.groupForPage(tool) && registry.pageById(QStringLiteral("test:tool"))==tool,
          "closing tool view parks its existing instance");
    tool->setText(QStringLiteral("background update"));
    check(registry.restorePage(tool,first) && registry.groupForPage(tool)==anchors[1]
          && tool->text()==QStringLiteral("background update"),"reopening restores tool location and background state");
    check(registry.restorePageInGroup(tool,first) && registry.groupForPage(tool)==anchors[1],
          "explicit restore activates an open page without relocating it");
    check(registry.parkPage(tool) && !registry.restorePageInGroup(tool,&foreign)
          && !registry.groupForPage(tool),"invalid explicit restore leaves the page parked");
    check(registry.restorePageInGroup(tool,first) && registry.groupForPage(tool)==first
          && tool->text()==QStringLiteral("background update"),"explicit restore overrides a parked page's return group without replacing state");
    registry.removeEmptyGroups();
    check(registry.groupCount()==4 && registry.regionGroup(QStringLiteral("main"))==first,
          "fixed region anchors remain available after their last page moves");
}

void testStableLayoutRoundTrip()
{
    QWidget window;
    auto* row = new QHBoxLayout(&window);
    auto* mainHost = new QWidget; row->addWidget(mainHost);
    auto* initial = UiControls::editorTabWidget(mainHost);
    EditorSplitController registry(initial); registry.setHost(mainHost);
    for (const auto& id : {QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("bottom")}) {
        auto* host = new QWidget; row->addWidget(host); registry.registerRegion(id, host);
    }
    auto* a = page("layout:a", "A"); auto* b = page("layout:b", "B"); auto* c = page("layout:c", "C");
    initial->addTab(a, "a"); initial->addTab(b, "b"); initial->addTab(c, "c");
    for (auto* item : {a, b, c}) registry.registerPage(item, {}, "code");
    auto* right = registry.createSplit(initial, EditorSplitDirection::Right);
    registry.movePage(b, right);
    auto* above = registry.createSplit(right, EditorSplitDirection::Above);
    registry.movePage(c, above);
    auto* tool = page("layout:tool", "Background");
    registry.regionGroup("left")->addTab(tool, "tool");
    registry.registerPage(tool, {}, "tool", [&] { return registry.parkPage(tool); });
    registry.parkPage(tool);
    window.resize(1300, 750); window.show(); QApplication::processEvents();
    registry.activatePage(c);
    const auto aGroup = registry.groupId(initial), bGroup = registry.groupId(right), cGroup = registry.groupId(above);
    const auto saved = registry.savePageLayout();
    for (auto* item : {a, b, c}) registry.movePage(item, registry.regionGroup("bottom"));
    registry.removeEmptyGroups();
    check(registry.restorePageLayout(saved).isEmpty(), "stable layout restores after original split widgets are removed");
    check(registry.groupId(registry.groupForPage(a)) == aGroup && registry.groupId(registry.groupForPage(b)) == bGroup
        && registry.groupId(registry.groupForPage(c)) == cGroup, "group identities survive reconstruction without replacing pages");
    auto* nested = qobject_cast<QSplitter*>(registry.groupForPage(c)->parentWidget());
    check(nested && nested->orientation() == Qt::Vertical && nested->widget(0) == registry.groupForPage(c)
        && nested->widget(1) == registry.groupForPage(b), "nested split orientation and order are restored");
    check(registry.activeGroup()->currentWidget() == c && registry.pageById("layout:tool") == tool
        && !registry.groupForPage(tool), "active page and parked background tool survive restoration");
    const int groupCount = registry.groupCount();
    check(registry.restorePageLayout(saved).isEmpty() && registry.groupCount() == groupCount,
        "repeated restore does not accumulate groups or duplicate views");
    check(registry.restorePage(tool, initial) && registry.regionForGroup(registry.groupForPage(tool)) == "left",
        "parked tool returns to its restored stable group");
    auto damaged = saved; auto regions = damaged.value("regions").toArray();
    for (int i = 0; i < regions.size(); ++i) {
        auto region = regions[i].toObject();
        if (region.value("id") == "left") { region.insert("tree", QJsonObject{}); regions[i] = region; }
    }
    damaged.insert("regions", regions);
    damaged.insert("hidden", QJsonArray{});
    check(!registry.restorePageLayout(damaged).isEmpty() && registry.groupForPage(tool)
        && registry.regionForGroup(registry.groupForPage(a)) == "main", "a damaged region does not discard valid regions or existing pages");
}

int main(int argc, char* argv[])
{
    qputenv(
        "QT_QPA_PLATFORM",
        QByteArrayLiteral("offscreen"));
    QApplication app(argc, argv);
    testUnifiedRegions();
    testStableLayoutRoundTrip();
    testPreviewGeometryAndDragVisual();
    testPreviewLifecycle();
    testDropResultMatchesPreview();
    testNestedSplitterDoesNotMutateBeforeDrop();
    testSharedDocumentViewsKeepIndependentState();

    QWidget host;
    auto* hostLayout = new QHBoxLayout(&host);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    auto* initial = new QTabWidget(&host);
    hostLayout->addWidget(initial);
    QWidget* first = page(QStringLiteral("view-a"),
                          QStringLiteral("A"));
    QWidget* second = page(QStringLiteral("view-b"),
                           QStringLiteral("B"));
    initial->addTab(first, QStringLiteral("a.sv"));
    initial->addTab(second, QStringLiteral("b.sv"));

    EditorSplitController controller(initial);
    controller.setHost(&host);
    host.resize(900, 600);
    host.show();
    QApplication::processEvents();

    const QList<EditorTabContextAction> initialContext =
        controller.tabContextActions(initial, 0);
    const QStringList expectedContextIds = {
        QString::fromLatin1(ActionIds::ViewEditorTabClose),
        QString::fromLatin1(ActionIds::ViewEditorTabCloseOthers),
        QString::fromLatin1(ActionIds::ViewEditorTabCloseRight),
        QString::fromLatin1(ActionIds::ViewEditorTabCloseAll),
        QString::fromLatin1(ActionIds::ViewReopenClosedTab),
        QString::fromLatin1(ActionIds::ViewEditorTabDuplicate),
        QString::fromLatin1(ActionIds::ViewTemporaryEditorOpen),
        QString::fromLatin1(ActionIds::ViewEditorSplitLeft),
        QString::fromLatin1(ActionIds::ViewEditorSplitRight),
        QString::fromLatin1(ActionIds::ViewEditorSplitAbove),
        QString::fromLatin1(ActionIds::ViewEditorSplitBelow),
        QString::fromLatin1(ActionIds::ViewEditorSplitMerge),
        QString::fromLatin1(ActionIds::ViewEditorTabToggleLocked),
    };
    QStringList actualContextIds;
    bool contextMetadataMatches =
        initialContext.size()
        == expectedContextIds.size();
    for (const EditorTabContextAction& item :
         initialContext) {
        actualContextIds.append(item.actionId);
        const ActionDescriptor* descriptor =
            findActionById(item.actionId);
        contextMetadataMatches =
            contextMetadataMatches
            && descriptor
            && item.executionRoute
                   == descriptor->executionRoute
            && item.label
                   == descriptor
                          ->aliasForSurface(
                              ActionSurface::TabContextMenu)
                          .label;
    }
    check(actualContextIds == expectedContextIds
              && contextMetadataMatches,
          "Tab context model consumes Registry ids, labels, and routes");
    check(initialContext.size() == 13
              && initialContext.value(6).enabled
              && !initialContext.value(11).enabled
              && initialContext.value(5).separatorBefore
              && initialContext.value(12).separatorBefore,
          "Tab context model preserves contextual enablement and grouping");
    first->setProperty("editorTabLocked", true);
    const QList<EditorTabContextAction> lockedContext =
        controller.tabContextActions(initial, 0);
    check(!lockedContext.isEmpty()
              && lockedContext.constLast().label
              == QStringLiteral("Unlock Tab"),
          "Tab context lock label follows selected view state");
    first->setProperty("editorTabLocked", false);

    QTabWidget* right = controller.createSplit(
        initial, EditorSplitDirection::Right);
    check(right && controller.groupCount() == 2,
          "right split creates a second group");
    const QList<EditorTabContextAction> splitContext =
        controller.tabContextActions(initial, 0);
    check(splitContext.size() > 10
              && splitContext.value(11).enabled,
          "Merge Group Action becomes available with multiple splits");
    check(controller.movePage(second, right)
              && initial->count() == 1
              && right->count() == 1,
          "a page moves across groups");

    QTabWidget* below = controller.movePageToSplit(
        first,
        EditorSplitDirection::Below,
        initial);
    check(below && controller.groupCount() == 3
              && below->currentWidget() == first,
          "moving the last page retains the fixed main region anchor");

    check(controller.handleDropForTest(
              QStringLiteral("view-b"),
              below,
              EditorSplitDirection::Left)
              && controller.groupCount() == 3,
          "edge drop creates a directional split");
    check(controller.groupForPage(second)
              != right,
          "edge drop moves the original page");

    controller.setActiveGroup(below);
    controller.toggleActiveGroupMaximized();
    QApplication::processEvents();
    check(controller.isGroupMaximized()
              && below->isVisible(),
          "current split can be temporarily maximized");
    controller.restoreGroupLayout();
    QApplication::processEvents();
    check(!controller.isGroupMaximized(),
          "maximized split restores the previous layout");

    controller.equalizeSplitSizes();
    bool equalized = true;
    for (QSplitter* splitter :
         host.findChildren<QSplitter*>()) {
        const QList<int> sizes = splitter->sizes();
        if (sizes.size() > 1) {
            const int minimum = *std::min_element(
                sizes.cbegin(), sizes.cend());
            const int maximum = *std::max_element(
                sizes.cbegin(), sizes.cend());
            equalized = equalized && maximum - minimum <= 2;
        }
    }
    check(equalized,
          "equalize assigns the same size within every splitter");

    check(controller.mergeGroup(
              controller.groupForPage(second),
              below),
          "a group can merge into another group");
    QApplication::processEvents();
    check(controller.groupForPage(second) == below,
          "merged pages share the destination group");

    auto* nestedTabs=new QTabWidget;
    nestedTabs->addTab(new QLabel("Nested tool page"),"Tool workflow");
    below->addTab(nestedTabs,"Tool");
    below->setCurrentWidget(nestedTabs);
    QTabBar unrelatedTabs;
    unrelatedTabs.addTab("Workspace");
    unrelatedTabs.show();
    QApplication::processEvents();
    for (auto* bar:{nestedTabs->tabBar(),&unrelatedTabs}) {
        int requested=0;
        bar->setContextMenuPolicy(Qt::CustomContextMenu);
        const auto connection=QObject::connect(bar,&QWidget::customContextMenuRequested,bar,
            [&](const QPoint&) { ++requested; });
        const auto point=bar->tabRect(0).center();
        QContextMenuEvent event(QContextMenuEvent::Mouse,point,bar->mapToGlobal(point));
        QApplication::sendEvent(bar,&event);
        QObject::disconnect(connection);
        check(requested==1,bar==&unrelatedTabs
            ? "unrelated workspace tabs retain their context menu"
            : "nested tool tabs retain their context menu");
    }

    std::cout << (checks - failures) << "/" << checks
              << " editor split checks passed\n";
    return failures == 0 ? 0 : 1;
}
