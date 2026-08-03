#include "editorsplitcontroller.h"

#include "actionregistry.h"

#include <QApplication>
#include <QBoxLayout>
#include <QContextMenuEvent>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QLayout>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {
constexpr const char* kEditorTabMime =
    "application/x-zeroslack-editor-tab";

bool directionComesBefore(EditorSplitDirection direction)
{
    return direction == EditorSplitDirection::Left
        || direction == EditorSplitDirection::Above;
}

Qt::Orientation orientationForDirection(
    EditorSplitDirection direction)
{
    return direction == EditorSplitDirection::Left
            || direction == EditorSplitDirection::Right
        ? Qt::Horizontal
        : Qt::Vertical;
}
}

EditorSplitController::EditorSplitController(
    QTabWidget* initial,
    QObject* parent)
    : QObject(parent)
    , rootWidget(initial)
    , firstGroup(initial)
    , currentGroup(initial)
{
    if (initial) {
        tabGroups.append(initial);
        configureGroup(initial);
    }
}

void EditorSplitController::setHost(QWidget* hostWidget)
{
    splitHost = hostWidget;
    if (!splitHost || !firstGroup)
        return;
    if (!splitHost->layout()) {
        auto* layout = new QVBoxLayout(splitHost);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
    }
    if (firstGroup->parentWidget() != splitHost
        && !qobject_cast<QSplitter*>(firstGroup->parentWidget())) {
        splitHost->layout()->addWidget(firstGroup);
    }
    rootWidget = firstGroup;
    bindDropTarget(splitHost);
}

QWidget* EditorSplitController::host() const
{
    return splitHost;
}

QTabWidget* EditorSplitController::initialGroup() const
{
    return firstGroup;
}

QTabWidget* EditorSplitController::activeGroup() const
{
    return currentGroup ? currentGroup.data() : firstGroup.data();
}

void EditorSplitController::setActiveGroup(QTabWidget* group)
{
    if (!group || !tabGroups.contains(group)
        || currentGroup == group) {
        return;
    }
    currentGroup = group;
    emit activeGroupChanged(group);
}

QList<QTabWidget*> EditorSplitController::groups() const
{
    QList<QTabWidget*> result;
    result.reserve(tabGroups.size());
    for (const QPointer<QTabWidget>& group : tabGroups) {
        if (group)
            result.append(group.data());
    }
    return result;
}

int EditorSplitController::groupCount() const
{
    return groups().size();
}

QTabWidget* EditorSplitController::groupForPage(QWidget* page) const
{
    if (!page)
        return nullptr;
    for (QTabWidget* group : groups()) {
        if (group->indexOf(page) >= 0)
            return group;
    }
    return nullptr;
}

QTabWidget* EditorSplitController::createSplit(
    QTabWidget* source,
    EditorSplitDirection direction)
{
    if (!source || !tabGroups.contains(source)
        || direction == EditorSplitDirection::Center) {
        return nullptr;
    }
    if (!splitHost)
        return nullptr;

    QTabWidget* created = createGroup();
    const Qt::Orientation orientation =
        orientationForDirection(direction);
    const bool before = directionComesBefore(direction);
    QSplitter* parentSplitter =
        qobject_cast<QSplitter*>(source->parentWidget());

    if (parentSplitter
        && parentSplitter->orientation() == orientation) {
        const int sourceIndex = parentSplitter->indexOf(source);
        parentSplitter->insertWidget(
            sourceIndex + (before ? 0 : 1),
            created);
    } else {
        auto* nested = new QSplitter(orientation);
        nested->setObjectName(QStringLiteral("editorSplit"));
        nested->setChildrenCollapsible(false);
        if (parentSplitter) {
            const int sourceIndex =
                parentSplitter->indexOf(source);
            parentSplitter->replaceWidget(sourceIndex, nested);
        } else if (splitHost->layout()) {
            splitHost->layout()->replaceWidget(source, nested);
            rootWidget = nested;
        }
        if (before) {
            nested->addWidget(created);
            nested->addWidget(source);
        } else {
            nested->addWidget(source);
            nested->addWidget(created);
        }
    }
    setActiveGroup(created);
    equalizeSplitSizes();
    emit layoutChanged();
    return created;
}

bool EditorSplitController::movePage(
    QWidget* page,
    QTabWidget* destination,
    int destinationIndex)
{
    QTabWidget* source = groupForPage(page);
    if (!page || !source || !destination
        || !tabGroups.contains(destination)) {
        return false;
    }
    const int sourceIndex = source->indexOf(page);
    if (sourceIndex < 0)
        return false;

    QWidget* takenPage = nullptr;
    const PagePresentation presentation =
        takePagePresentation(source, sourceIndex, &takenPage);
    if (!takenPage)
        return false;
    source->removeTab(sourceIndex);
    insertPage(destination,
               takenPage,
               presentation,
               destinationIndex);
    setActiveGroup(destination);
    emit pageMoved(page, source, destination);
    removeEmptyGroups();
    emit layoutChanged();
    return true;
}

QTabWidget* EditorSplitController::movePageToSplit(
    QWidget* page,
    EditorSplitDirection direction,
    QTabWidget* relativeTo)
{
    QTabWidget* source =
        relativeTo ? relativeTo : groupForPage(page);
    QTabWidget* created = createSplit(source, direction);
    if (!created)
        return nullptr;
    if (!movePage(page, created)) {
        removeEmptyGroups();
        return nullptr;
    }
    return created;
}

bool EditorSplitController::mergeGroup(
    QTabWidget* source,
    QTabWidget* destination)
{
    if (!source || !tabGroups.contains(source)
        || tabGroups.size() <= 1) {
        return false;
    }
    if (!destination)
        destination = fallbackMergeDestination(source);
    if (!destination || destination == source)
        return false;

    while (source->count() > 0) {
        QWidget* page = source->widget(0);
        if (!movePage(page, destination))
            return false;
    }
    removeEmptyGroups();
    setActiveGroup(destination);
    return true;
}

void EditorSplitController::removeEmptyGroups()
{
    if (tabGroups.size() <= 1)
        return;
    const QList<QPointer<QTabWidget>> snapshot = tabGroups;
    for (const QPointer<QTabWidget>& groupPointer : snapshot) {
        QTabWidget* group = groupPointer;
        if (!group || group->count() != 0
            || tabGroups.size() <= 1) {
            continue;
        }
        QSplitter* parentSplitter =
            qobject_cast<QSplitter*>(group->parentWidget());
        tabGroups.removeAll(group);
        if (currentGroup == group)
            currentGroup = firstGroup;
        group->setParent(nullptr);
        group->deleteLater();
        collapseRedundantSplitter(parentSplitter);
        emit groupRemoved();
    }
}

bool EditorSplitController::isGroupMaximized() const
{
    return groupMaximized;
}

void EditorSplitController::toggleActiveGroupMaximized()
{
    if (groupMaximized) {
        restoreGroupLayout();
        return;
    }
    QTabWidget* active = activeGroup();
    if (!active || tabGroups.size() < 2)
        return;

    savedSplitterSizes.clear();
    savedGroupVisibility.clear();
    collectSplitterSizes(rootWidget);
    for (QTabWidget* group : groups()) {
        savedGroupVisibility.insert(group, group->isVisible());
        if (group != active)
            group->hide();
    }
    active->show();
    groupMaximized = true;
    emit layoutChanged();
}

void EditorSplitController::restoreGroupLayout()
{
    if (!groupMaximized)
        return;
    for (QTabWidget* group : groups()) {
        group->setVisible(
            savedGroupVisibility.value(group, true));
    }
    restoreSplitterSizes(rootWidget);
    savedSplitterSizes.clear();
    savedGroupVisibility.clear();
    groupMaximized = false;
    emit layoutChanged();
}

void EditorSplitController::equalizeSplitSizes()
{
    equalizeSplitter(rootWidget);
    emit layoutChanged();
}

bool EditorSplitController::handleDropForTest(
    const QString& viewId,
    QTabWidget* target,
    EditorSplitDirection direction)
{
    return handleTabDrop(viewId, target, direction);
}

bool EditorSplitController::eventFilter(
    QObject* watched,
    QEvent* event)
{
    QTabBar* bar = qobject_cast<QTabBar*>(watched);
    if (bar && event->type() == QEvent::MouseButtonPress) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) {
            pressedBar = bar;
            dragStartPosition = mouse->position().toPoint();
            pressedTabIndex = bar->tabAt(dragStartPosition);
        }
    } else if (bar && event->type() == QEvent::MouseMove) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (startTabDrag(bar, mouse))
            return true;
    } else if (bar && event->type() == QEvent::ContextMenu) {
        auto* context = static_cast<QContextMenuEvent*>(event);
        showTabContextMenu(bar, context->pos());
        return true;
    }

    if (event->type() == QEvent::DragEnter
        || event->type() == QEvent::DragMove) {
        auto* drag = static_cast<QDragMoveEvent*>(event);
        if (drag->mimeData()->hasFormat(
                QString::fromLatin1(kEditorTabMime))) {
            drag->acceptProposedAction();
            return true;
        }
    } else if (event->type() == QEvent::Drop) {
        auto* drop = static_cast<QDropEvent*>(event);
        if (!drop->mimeData()->hasFormat(
                QString::fromLatin1(kEditorTabMime))) {
            return QObject::eventFilter(watched, event);
        }
        QTabWidget* target = groupForObject(watched);
        if (!target)
            target = activeGroup();
        const QPoint localPosition =
            target
            ? target->mapFromGlobal(
                  static_cast<QWidget*>(watched)
                      ->mapToGlobal(
                          drop->position().toPoint()))
            : QPoint();
        const EditorSplitDirection direction =
            target
            ? dropDirection(target, localPosition)
            : EditorSplitDirection::Center;
        const QString viewId = QString::fromUtf8(
            drop->mimeData()->data(
                QString::fromLatin1(kEditorTabMime)));
        if (handleTabDrop(viewId, target, direction)) {
            drop->acceptProposedAction();
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}

QTabWidget* EditorSplitController::createGroup()
{
    auto* group = new QTabWidget(splitHost);
    configureGroup(group);
    tabGroups.append(group);
    emit groupCreated(group);
    return group;
}

void EditorSplitController::configureGroup(QTabWidget* group)
{
    if (!group)
        return;
    group->setObjectName(
        QStringLiteral("editorTabGroup%1")
            .arg(tabGroups.size() + 1));
    group->setDocumentMode(true);
    group->setTabsClosable(true);
    group->setMovable(true);
    group->setElideMode(Qt::ElideMiddle);
    group->setAcceptDrops(true);
    group->installEventFilter(this);
    if (QTabBar* bar = group->tabBar()) {
        bar->setAcceptDrops(true);
        bar->installEventFilter(this);
    }
    connect(group,
            &QTabWidget::currentChanged,
            this,
            [this, group](int) {
                setActiveGroup(group);
            });
    connect(group,
            &QTabWidget::tabCloseRequested,
            this,
            [this, group](int index) {
                emit tabCloseRequested(group, index);
            });
    bindDropTarget(group);
}

void EditorSplitController::bindDropTarget(QWidget* target)
{
    if (!target)
        return;
    target->setAcceptDrops(true);
    target->installEventFilter(this);
}

QTabWidget* EditorSplitController::groupForObject(QObject* object) const
{
    QWidget* widget = qobject_cast<QWidget*>(object);
    while (widget) {
        if (auto* group = qobject_cast<QTabWidget*>(widget)) {
            if (tabGroups.contains(group))
                return group;
        }
        widget = widget->parentWidget();
    }
    return nullptr;
}

QWidget* EditorSplitController::pageForViewId(
    const QString& viewId) const
{
    if (viewId.isEmpty())
        return nullptr;
    for (QTabWidget* group : groups()) {
        for (int index = 0; index < group->count(); ++index) {
            QWidget* page = group->widget(index);
            if (viewIdForPage(page) == viewId)
                return page;
        }
    }
    return nullptr;
}

QString EditorSplitController::viewIdForPage(QWidget* page) const
{
    return page
        ? page->property("editorViewId").toString()
        : QString();
}

EditorSplitController::PagePresentation
EditorSplitController::takePagePresentation(
    QTabWidget* group,
    int index,
    QWidget** page) const
{
    PagePresentation presentation;
    if (page)
        *page = nullptr;
    if (!group || index < 0 || index >= group->count())
        return presentation;
    if (page)
        *page = group->widget(index);
    presentation.text = group->tabText(index);
    presentation.icon = group->tabIcon(index);
    presentation.toolTip = group->tabToolTip(index);
    presentation.data = group->tabBar()->tabData(index);
    return presentation;
}

void EditorSplitController::insertPage(
    QTabWidget* group,
    QWidget* page,
    const PagePresentation& presentation,
    int destinationIndex)
{
    if (!group || !page)
        return;
    const int boundedIndex =
        destinationIndex < 0
        ? group->count()
        : qBound(0, destinationIndex, group->count());
    const int inserted = group->insertTab(
        boundedIndex,
        page,
        presentation.icon,
        presentation.text);
    group->setTabToolTip(inserted, presentation.toolTip);
    group->tabBar()->setTabData(inserted, presentation.data);
    group->setCurrentIndex(inserted);
}

void EditorSplitController::collapseRedundantSplitter(
    QSplitter* splitter)
{
    if (!splitter || splitter->count() != 1)
        return;
    QWidget* survivor = splitter->widget(0);
    if (!survivor)
        return;
    QSplitter* parentSplitter =
        qobject_cast<QSplitter*>(splitter->parentWidget());
    if (parentSplitter) {
        const int index = parentSplitter->indexOf(splitter);
        parentSplitter->replaceWidget(index, survivor);
    } else if (splitHost && splitHost->layout()) {
        splitHost->layout()->replaceWidget(splitter, survivor);
        rootWidget = survivor;
    }
    splitter->setParent(nullptr);
    splitter->deleteLater();
    collapseRedundantSplitter(parentSplitter);
}

QTabWidget* EditorSplitController::fallbackMergeDestination(
    QTabWidget* source) const
{
    for (QTabWidget* group : groups()) {
        if (group != source)
            return group;
    }
    return nullptr;
}

void EditorSplitController::collectSplitterSizes(QWidget* widget)
{
    auto* splitter = qobject_cast<QSplitter*>(widget);
    if (!splitter)
        return;
    savedSplitterSizes.insert(splitter, splitter->sizes());
    for (int index = 0; index < splitter->count(); ++index)
        collectSplitterSizes(splitter->widget(index));
}

void EditorSplitController::restoreSplitterSizes(QWidget* widget)
{
    auto* splitter = qobject_cast<QSplitter*>(widget);
    if (!splitter)
        return;
    const QList<int> sizes = savedSplitterSizes.value(splitter);
    if (sizes.size() == splitter->count())
        splitter->setSizes(sizes);
    for (int index = 0; index < splitter->count(); ++index)
        restoreSplitterSizes(splitter->widget(index));
}

void EditorSplitController::equalizeSplitter(QWidget* widget)
{
    auto* splitter = qobject_cast<QSplitter*>(widget);
    if (!splitter)
        return;
    splitter->setSizes(QList<int>(splitter->count(), 1));
    for (int index = 0; index < splitter->count(); ++index)
        equalizeSplitter(splitter->widget(index));
}

EditorSplitDirection EditorSplitController::dropDirection(
    QTabWidget* target,
    const QPoint& position) const
{
    if (!target)
        return EditorSplitDirection::Center;
    const QRect rect = target->rect();
    const int horizontalEdge = qMin(90, qMax(24, rect.width() / 4));
    const int verticalEdge = qMin(90, qMax(24, rect.height() / 4));
    if (position.x() <= horizontalEdge)
        return EditorSplitDirection::Left;
    if (position.x() >= rect.width() - horizontalEdge)
        return EditorSplitDirection::Right;
    if (position.y() <= verticalEdge)
        return EditorSplitDirection::Above;
    if (position.y() >= rect.height() - verticalEdge)
        return EditorSplitDirection::Below;
    return EditorSplitDirection::Center;
}

bool EditorSplitController::startTabDrag(
    QTabBar* bar,
    QMouseEvent* event)
{
    if (!bar || bar != pressedBar
        || pressedTabIndex < 0
        || !(event->buttons() & Qt::LeftButton)) {
        return false;
    }
    const QPoint current = event->position().toPoint();
    if ((current - dragStartPosition).manhattanLength()
        < QApplication::startDragDistance()) {
        return false;
    }
    if (bar->rect().adjusted(-8, -8, 8, 8).contains(current))
        return false;

    QTabWidget* group = groupForObject(bar);
    QWidget* page =
        group && pressedTabIndex < group->count()
        ? group->widget(pressedTabIndex)
        : nullptr;
    const QString viewId = viewIdForPage(page);
    if (viewId.isEmpty())
        return false;

    auto* mime = new QMimeData;
    mime->setData(
        QString::fromLatin1(kEditorTabMime),
        viewId.toUtf8());
    auto* drag = new QDrag(bar);
    drag->setMimeData(mime);
    drag->exec(Qt::MoveAction);
    pressedBar = nullptr;
    pressedTabIndex = -1;
    return true;
}

bool EditorSplitController::handleTabDrop(
    const QString& viewId,
    QTabWidget* target,
    EditorSplitDirection direction)
{
    QWidget* page = pageForViewId(viewId);
    if (!page || !target)
        return false;
    QTabWidget* source = groupForPage(page);
    if (!source)
        return false;

    if (direction == EditorSplitDirection::Center) {
        if (source == target)
            return true;
        return movePage(page, target);
    }
    QTabWidget* created = createSplit(target, direction);
    return created && movePage(page, created);
}

QList<EditorTabContextAction>
EditorSplitController::tabContextActions(
    QTabWidget* group,
    int index) const
{
    struct Spec {
        const char* actionId;
        bool requiresTab;
        bool requiresMultipleGroups;
        bool separatorBefore;
    };
    const Spec specs[] = {
        {ActionIds::ViewEditorTabClose, true, false, false},
        {ActionIds::ViewEditorTabCloseOthers, true, false, false},
        {ActionIds::ViewEditorTabCloseRight, true, false, false},
        {ActionIds::ViewEditorTabCloseAll, false, false, false},
        {ActionIds::ViewReopenClosedTab, false, false, false},
        {ActionIds::ViewEditorTabDuplicate, true, false, true},
        {ActionIds::ViewEditorSplitLeft, true, false, false},
        {ActionIds::ViewEditorSplitRight, true, false, false},
        {ActionIds::ViewEditorSplitAbove, true, false, false},
        {ActionIds::ViewEditorSplitBelow, true, false, false},
        {ActionIds::ViewEditorSplitMerge, false, true, false},
        {ActionIds::ViewEditorTabToggleLocked, true, false, true},
    };
    const bool hasTab = group
        && index >= 0
        && index < group->count();
    const QWidget* page = hasTab
        ? group->widget(index)
        : nullptr;
    QList<EditorTabContextAction> result;
    result.reserve(
        static_cast<qsizetype>(
            sizeof(specs) / sizeof(specs[0])));
    for (const Spec& spec : specs) {
        const ActionDescriptor* descriptor =
            findActionById(
                QString::fromLatin1(spec.actionId));
        if (!descriptor)
            continue;
        const ActionAliasDescriptor tabAlias =
            descriptor->aliasForSurface(
                ActionSurface::TabContextMenu);
        if (tabAlias.token.isEmpty())
            continue;
        EditorTabContextAction item;
        item.actionId = descriptor->id;
        item.label = tabAlias.label.isEmpty()
            ? descriptor->canonicalName
            : tabAlias.label;
        if (descriptor->id
                == QString::fromLatin1(
                    ActionIds::ViewEditorTabToggleLocked)
            && page
            && page->property(
                   "editorTabLocked").toBool()) {
            item.label = QStringLiteral("Unlock Tab");
        }
        item.executionRoute =
            descriptor->executionRoute;
        item.enabled =
            (!spec.requiresTab || hasTab)
            && (!spec.requiresMultipleGroups
                || tabGroups.size() > 1);
        item.separatorBefore =
            spec.separatorBefore;
        result.append(item);
    }
    return result;
}

void EditorSplitController::showTabContextMenu(
    QTabBar* bar,
    const QPoint& position)
{
    QTabWidget* group = groupForObject(bar);
    if (!group)
        return;
    const int index = bar->tabAt(position);

    QMenu menu(bar);
    for (const EditorTabContextAction& item :
         tabContextActions(group, index)) {
        if (item.separatorBefore)
            menu.addSeparator();
        QAction* action = menu.addAction(item.label);
        action->setObjectName(
            QStringLiteral("tabContext.%1")
                .arg(item.actionId));
        action->setProperty(
            "actionId", item.actionId);
        action->setProperty(
            "executionRoute",
            item.executionRoute);
        action->setEnabled(item.enabled);
    }

    QAction* selected = menu.exec(bar->mapToGlobal(position));
    if (!selected)
        return;
    const QString actionId =
        selected->property("actionId").toString();
    if (!actionId.isEmpty()) {
        emit tabActionRequested(
            actionId, group, index);
    }
}
