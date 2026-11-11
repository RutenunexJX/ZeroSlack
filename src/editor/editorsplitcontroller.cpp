#include <memory>
#include "editorsplitcontroller.h"

#include "actionregistry.h"
#include "editordroppreviewoverlay.h"
#include "uicontrols.h"
#ifdef ZEROSLACK_ENABLE_ELA
#include "ElaTabWidget.h"
#include "ElaTabBar.h"
#endif

#include <QAbstractScrollArea>
#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QChildEvent>
#include <QContextMenuEvent>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QLayout>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPixmap>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QWidget>
#include <QVBoxLayout>
#include <QUuid>
#include <QSignalBlocker>
#include <QJsonArray>
#include <QScopedValueRollback>
#include <QScreen>
#include <QSet>
#include <QStyle>

#include <algorithm>
#include <utility>

namespace {
constexpr const char* kEditorTabMime =
    "application/x-zeroslack-editor-tab";

bool isFloatingGroup(QTabWidget* group)
{
#ifdef ZEROSLACK_ENABLE_ELA
    if (auto* ela = qobject_cast<ElaTabWidget*>(group))
        return ela->isFloatingTabWidget();
#endif
    return false;
}

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
    qApp->installEventFilter(this);
    if (initial) {
        regionHosts.insert(QStringLiteral("main"), {nullptr, initial, initial, initial, {}});
        groupRegions.insert(initial, QStringLiteral("main"));
        tabGroups.append(initial);
        configureGroup(initial);
    }
}

EditorSplitController::~EditorSplitController()
{
    clearDropPreview();
    if (dropPreview)
        delete dropPreview.data();
}

void EditorSplitController::setHost(QWidget* hostWidget)
{
    splitHost = hostWidget;
    registerRegion(QStringLiteral("main"), hostWidget, firstGroup);
}

QTabWidget* EditorSplitController::registerRegion(const QString& id, QWidget* hostWidget, QTabWidget* initial)
{
    if (id.isEmpty() || !hostWidget) return nullptr;
    auto found = regionHosts.find(id);
    if (found != regionHosts.end() && found->host && found->host != hostWidget) return nullptr;
    if (initial && tabGroups.contains(initial) && groupRegions.value(initial) != id) return nullptr;
    auto& region = regionHosts[id];
    region.host = hostWidget;
    if (!hostWidget->layout()) {
        auto* layout = new QVBoxLayout(hostWidget);
        layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    }
    if (!region.anchor) {
        auto* created = initial ? initial : UiControls::editorTabWidget(hostWidget);
        region.anchor = created; region.root = created; region.current = created;
        groupRegions.insert(created,id);
        if (!tabGroups.contains(created)) {
            tabGroups.append(created); configureGroup(created); emit groupCreated(created);
        }
    }
    if (!region.root) region.root = region.anchor;
    if (hostWidget->layout()->indexOf(region.root) < 0)
        hostWidget->layout()->addWidget(region.root);
    if (id == QStringLiteral("main")) {
        firstGroup = region.anchor; splitHost = hostWidget; rootWidget = region.root;
    }
    bindDropTarget(hostWidget);
    return region.anchor;
}

QJsonObject EditorSplitController::savePageLayout(const std::function<bool(QWidget*)>& include) const
{
    const auto included = [&](QWidget* page) { return page && (!include || include(page)); };
    const auto groupState = [&](QTabWidget* group) {
        QJsonArray order;
        for (int i = 0; i < group->count(); ++i)
            if (included(group->widget(i))) order.append(pageId(group->widget(i)));
        return QJsonObject{{"id", groupId(group)}, {"pages", order},
            {"current", included(group->currentWidget()) ? pageId(group->currentWidget()) : QString()},
            {"anchor", regionGroup(regionForGroup(group)) == group}};
    };
    std::function<QJsonObject(QWidget*)> tree = [&](QWidget* node) -> QJsonObject {
        if (auto* group = qobject_cast<QTabWidget*>(node)) return groupState(group);
        auto* splitter = qobject_cast<QSplitter*>(node);
        if (!splitter) return {};
        QJsonArray children, sizes;
        for (int i = 0; i < splitter->count(); ++i) children.append(tree(splitter->widget(i)));
        for (int size : savedSplitterSizes.value(splitter, splitter->sizes())) sizes.append(size);
        return {{"orientation", splitter->orientation() == Qt::Horizontal ? "horizontal" : "vertical"},
                {"children", children}, {"sizes", sizes}};
    };
    QJsonArray areas, floating, hidden;
    QJsonObject types;
    for (auto it = pages.cbegin(); it != pages.cend(); ++it)
        if (included(it.key())) types.insert(it->id, it.key()->property("pageKind").toString());
    for (const auto& id : regions()) {
        const auto region = regionHosts.value(id);
        areas.append(QJsonObject{{"id", id}, {"tree", tree(region.root)}, {"activeGroup", groupId(region.current)}});
    }
    for (auto* group : groups()) if (isFloatingGroup(group)) {
        auto state = groupState(group);
        if (state.value("pages").toArray().isEmpty()) continue;
        const auto rect = group->window()->geometry();
        state.insert("region", regionForGroup(group));
        state.insert("geometry", QJsonArray{rect.x(), rect.y(), rect.width(), rect.height()});
        floating.append(state);
    }
    for (auto it = pages.cbegin(); it != pages.cend(); ++it)
        if (!groupForPage(it.key()) && included(it.key()) && !it->returnGroup.isEmpty())
            hidden.append(QJsonObject{{"id", it->id}, {"group", it->returnGroup}, {"index", it->returnIndex}});
    return {{"version", 1}, {"regions", areas}, {"floating", floating}, {"hidden", hidden}, {"pageTypes", types},
        {"activePage", currentGroup && included(currentGroup->currentWidget()) ? pageId(currentGroup->currentWidget()) : QString()},
        {"maximizedGroup", groupMaximized ? groupId(currentGroup) : QString()}};
}

QStringList EditorSplitController::restorePageLayout(const QJsonObject& layout)
{
    QStringList warnings;
    if (layout.isEmpty()) return warnings;
    if (layout.value("version").toInt() != 1) return {QStringLiteral("Unsupported page layout")};
    const QPointer<QWidget> previousActive=currentGroup ? currentGroup->currentWidget() : nullptr;
    QSet<QString> identities, restoredPages;
    int nodes = 0;
    std::function<QJsonObject(const QJsonObject&, int)> validate = [&](const QJsonObject& node, int depth) -> QJsonObject {
        if (++nodes > 512 || depth > 16) { warnings.append(QStringLiteral("Page layout limit exceeded")); return {}; }
        const auto id = node.value("id").toString();
        if (!id.isEmpty() && id.size() <= 128 && !identities.contains(id)) {
            identities.insert(id); return node;
        }
        if (node.contains("children") && (node.value("orientation") == "horizontal" || node.value("orientation") == "vertical")) {
            QJsonArray children;
            for (const auto& value : node.value("children").toArray()) {
                const auto child = validate(value.toObject(), depth + 1);
                if (!child.isEmpty()) children.append(child);
            }
            if (children.size() == 1) return children.first().toObject();
            if (!children.isEmpty()) { auto result = node; result.insert("children", children); return result; }
        }
        warnings.append(QStringLiteral("Invalid or duplicate page group skipped")); return {};
    };
    QJsonArray validRegions;
    QSet<QString> regionIds;
    for (const auto& value : layout.value("regions").toArray()) {
        auto region = value.toObject(); const auto id = region.value("id").toString();
        if (!regionHosts.contains(id) || regionIds.contains(id)) { warnings.append(QStringLiteral("Unknown page region skipped")); continue; }
        const auto tree = validate(region.value("tree").toObject(), 0);
        if (!tree.isEmpty()) { region.insert("tree", tree); validRegions.append(region); regionIds.insert(id); }
    }
    if (validRegions.isEmpty()) return warnings;
    if (groupMaximized) restoreGroupLayout();
    const QScopedValueRollback restoring(restoringPageLayout, true);
    QList<QPair<QPointer<QTabWidget>, QJsonObject>> leaves;
    QList<QPair<QPointer<QSplitter>, QList<int>>> restoredSizes;
    for (const auto& value : validRegions) {
        const auto state = value.toObject(); const auto id = state.value("id").toString();
        auto& region = regionHosts[id];
        QPointer<QWidget> oldRoot = region.root;
        const auto oldGroups = groups(id);
        QSet<QTabWidget*> used;
        bool anchorUsed = false;
        std::function<QWidget*(const QJsonObject&)> build = [&](const QJsonObject& node) -> QWidget* {
            if (node.contains("id")) {
                QTabWidget* group = nullptr;
                const auto identity = node.value("id").toString();
                if (node.value("anchor").toBool() && !anchorUsed) group = region.anchor;
                if (!group) {
                    auto* existing = groupById(identity);
                    if (existing && regionForGroup(existing) == id && !isFloatingGroup(existing) && !used.contains(existing)) group = existing;
                }
                if (!group) group = createGroup(id);
                if (group == region.anchor) anchorUsed = true;
                used.insert(group); group->setProperty("pageGroupId", identity);
                leaves.append({group, node}); return group;
            }
            auto* splitter = new QSplitter(node.value("orientation") == "horizontal" ? Qt::Horizontal : Qt::Vertical);
            splitter->setObjectName(QStringLiteral("editorSplit")); splitter->setChildrenCollapsible(false);
            connect(splitter, &QSplitter::splitterMoved, this, [this, splitter] { emit splitterResized(splitter); emit layoutChanged(); });
            for (const auto& child : node.value("children").toArray()) { auto* widget = build(child.toObject()); splitter->addWidget(widget); widget->show(); }
            QList<int> sizes;
            for (const auto& size : node.value("sizes").toArray()) sizes.append(qBound(1, size.toInt(1), 100000));
            if (sizes.size() == splitter->count()) restoredSizes.append({splitter, sizes});
            return splitter;
        };
        QWidget* root = build(state.value("tree").toObject());
        if (!anchorUsed) {
            // Keep the fixed region's public anchor even when a damaged record
            // lost its leaf. Existing page pointers and navigation owners survive.
            auto* wrapper = new QSplitter(Qt::Horizontal);
            wrapper->setObjectName(QStringLiteral("editorSplit")); wrapper->setChildrenCollapsible(false);
            connect(wrapper, &QSplitter::splitterMoved, this, [this, wrapper] { emit splitterResized(wrapper); emit layoutChanged(); });
            wrapper->addWidget(region.anchor); wrapper->addWidget(root); region.anchor->show(); root->show();
            used.insert(region.anchor); root = wrapper;
        }
        if (oldRoot) region.host->layout()->removeWidget(oldRoot);
        region.host->layout()->addWidget(root); updateRegionRoot(id, root); root->show();
        for (auto* group : oldGroups) {
            if (used.contains(group) || isFloatingGroup(group)) continue;
            while (group->count()) {
                if (!movePage(group->widget(0), region.anchor)) { warnings.append(QStringLiteral("Page transfer failed during layout restore")); break; }
            }
            if (!group->count()) { group->setParent(nullptr); delete group; }
        }
        if (oldRoot && oldRoot != root && qobject_cast<QSplitter*>(oldRoot.data())) delete oldRoot;
    }
    for (const auto& value : layout.value("floating").toArray()) {
        const auto state = validate(value.toObject(), 0); const auto id = state.value("region").toString();
        if (state.isEmpty() || !regionHosts.contains(id)) continue;
        auto* group = groupById(state.value("id").toString());
        if (!group || !isFloatingGroup(group)) {
            QWidget* first = nullptr;
            for (const auto& name : state.value("pages").toArray()) if ((first = pageById(name.toString()))) break;
            if (!first) continue;
            group = floatPage(first, QGuiApplication::primaryScreen()->availableGeometry().topLeft());
            if (!group) continue;
        }
        groupRegions[group] = id; group->setProperty("pageGroupId", state.value("id").toString());
        const auto geometry = state.value("geometry").toArray();
        if (geometry.size() == 4) {
            const auto available = group->screen()->availableGeometry();
            const int width = qBound(180, geometry[2].toInt(640), available.width());
            const int height = qBound(150, geometry[3].toInt(480), available.height());
            group->window()->setGeometry(qBound(available.left(), geometry[0].toInt(), available.right() - width + 1),
                qBound(available.top(), geometry[1].toInt(), available.bottom() - height + 1), width, height);
        }
        leaves.append({group, state});
    }
    for (const auto& [guard, state] : leaves) {
        if (!guard) continue;
        int index = 0;
        for (const auto& name : state.value("pages").toArray()) {
            const auto identity = name.toString(); auto* page = pageById(identity);
            if (!page || restoredPages.contains(identity)) continue;
            restoredPages.insert(identity);
            if (!groupForPage(page) && !restorePage(page, guard, false)) continue;
            movePage(page, guard, index++);
        }
    }
    for (const auto& value : layout.value("hidden").toArray()) {
        const auto state = value.toObject(); auto* page = pageById(state.value("id").toString());
        if (!page || !pages.contains(page)) continue;
        if (groupForPage(page)) parkPage(page);
        auto& stored = pages[page]; stored.returnGroup = state.value("group").toString(); stored.returnIndex = qMax(0, state.value("index").toInt());
    }
    // Moving later pages can change the current tab of an earlier group.
    // Apply every selection only after all placements and parking are complete.
    for (const auto& [guard, state] : leaves) {
        auto* selected = pageById(state.value("current").toString());
        if (guard && selected && guard->indexOf(selected) >= 0) {
            const QSignalBlocker block(guard);
            guard->setCurrentWidget(selected);
        }
    }
    for (const auto& value : validRegions) {
        const auto state = value.toObject(); const auto id = state.value("id").toString();
        auto& region = regionHosts[id];
        region.current = groupById(state.value("activeGroup").toString());
        if (!region.current || regionForGroup(region.current) != id) region.current = region.anchor;
    }
    if (auto* active = pageById(layout.value("activePage").toString()); active && groupForPage(active)) {
        activatePage(active, false);
    } else if (previousActive && groupForPage(previousActive)) {
        activatePage(previousActive,false);
    } else if (!currentGroup) setActiveGroup(firstGroup);
    // Empty/reparented groups have transient minimum sizes. Restore ratios only
    // after their pages and the region layout are in their final positions.
    for (const auto& value : validRegions) {
        const auto& region = regionHosts[value.toObject().value("id").toString()];
        if (region.host->window()->layout()) region.host->window()->layout()->activate();
        if (region.host->layout()) region.host->layout()->activate();
    }
    for (auto it = restoredSizes.crbegin(); it != restoredSizes.crend(); ++it)
        if (it->first) it->first->setSizes(it->second);
    if (auto* group = groupById(layout.value("maximizedGroup").toString())) { setActiveGroup(group); toggleActiveGroupMaximized(); }
    syncFloatingVisibility(); emit layoutChanged();
    warnings.removeDuplicates(); return warnings;
}

QStringList EditorSplitController::regions() const
{
    auto ids = regionHosts.keys(); ids.sort(); return ids;
}
QString EditorSplitController::regionForGroup(QTabWidget* group) const { return groupRegions.value(group); }
QTabWidget* EditorSplitController::regionGroup(const QString& id) const { return regionHosts.value(id).anchor; }
QTabWidget* EditorSplitController::regionActiveGroup(const QString& id) const
{
    const auto region=regionHosts.value(id);
    return region.current && tabGroups.contains(region.current) ? region.current.data() : region.anchor.data();
}
QWidget* EditorSplitController::regionRoot(const QString& id) const { return regionHosts.value(id).root; }
QWidget* EditorSplitController::regionHost(const QString& id) const { return regionHosts.value(id).host; }
bool EditorSplitController::regionHasPages(const QString& id) const
{
    for (auto* group : groups(id)) if (group->count()) return true;
    return false;
}
bool EditorSplitController::takePage(QWidget* page)
{
    if (!page) return false;
    if (auto* group=groupForPage(page)) {
        group->removeTab(group->indexOf(page));
        page->hide();
    }
    unregisterPage(page);
    removeEmptyGroups();
    emit pageVisibilityChanged(page,false); emit layoutChanged();
    return true;
}
QList<QTabWidget*> EditorSplitController::groups(const QString& id) const
{
    QList<QTabWidget*> result;
    for (auto* group : groups()) if (regionForGroup(group) == id) result.append(group);
    return result;
}
QString EditorSplitController::groupId(QTabWidget* group) const
{
    return group && tabGroups.contains(group) ? group->property("pageGroupId").toString() : QString();
}
QTabWidget* EditorSplitController::groupById(const QString& id) const
{
    if (id.isEmpty()) return nullptr;
    for (auto* group : groups()) if (groupId(group) == id) return group;
    return nullptr;
}
QString EditorSplitController::pageId(QWidget* page) const
{
    if (!page) return {};
    QString id = page->property("pageId").toString();
    if (id.isEmpty()) {
        id = page->property("editorViewId").toString();
        if (id.isEmpty()) id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        page->setProperty("pageId", id);
    }
    return id;
}
QWidget* EditorSplitController::pageById(const QString& id) const
{
    if (id.isEmpty()) return nullptr;
    for (auto it=pages.cbegin(); it!=pages.cend(); ++it) if (it->id==id) return it.key();
    for (auto* group : groups())
        for (int i=0;i<group->count();++i) if (pageId(group->widget(i))==id) return group->widget(i);
    return nullptr;
}
bool EditorSplitController::registerPage(QWidget* page, const QString& id, const QString& kind,
                                         std::function<bool()> close)
{
    if (!page) return false;
    const QString identity=id.isEmpty() ? pageId(page) : id;
    if (auto* other=pageById(identity); other && other!=page) return false;
    if (!pages.contains(page)) connect(page, &QObject::destroyed, this, [this,page] { pages.remove(page); });
    auto& entry=pages[page]; entry.id=identity;
    page->setProperty("pageId",identity);
    if (!kind.isEmpty()) page->setProperty("pageKind",kind);
    if (close) entry.close=std::move(close);
    return true;
}
void EditorSplitController::unregisterPage(QWidget* page)
{
    pages.remove(page);
}
bool EditorSplitController::parkPage(QWidget* page)
{
    auto* group=groupForPage(page); if (!group) return false;
    if (!pages.contains(page) && !registerPage(page)) return false;
    const int index = group->indexOf(page);
    auto& entry=pages[page]; entry.returnGroup=groupId(group); entry.returnIndex=index;
    entry.presentation=takePagePresentation(group,index,nullptr);
    if (!parkedPages) {
        parkedPages=new QWidget(splitHost ? splitHost->window() : regionHosts.value(regionForGroup(group)).host->window());
        parkedPages->setObjectName(QStringLiteral("parkedToolPages")); parkedPages->hide();
    }
    group->removeTab(index);
    page->hide(); page->setParent(parkedPages);
    removeEmptyGroups();
    emit pageVisibilityChanged(page,false); emit layoutChanged();
    return true;
}
bool EditorSplitController::restorePage(QWidget* page, QTabWidget* fallback, bool activate)
{
    if (!page || !pages.contains(page)) return false;
    if (groupForPage(page)) return activate ? activatePage(page) : true;
    const auto entry=pages.value(page);
    auto* group=groupById(entry.returnGroup); if (!group) group=fallback;
    if (!group || !tabGroups.contains(group)) return false;
    {
        const QSignalBlocker block(group);
        const int previous=group->currentIndex();
        insertPage(group,page,entry.presentation,entry.returnIndex);
        if (!activate && previous>=0) group->setCurrentIndex(previous);
    }
    emit pageVisibilityChanged(page,true); emit layoutChanged();
    return activate ? activatePage(page) : true;
}
bool EditorSplitController::hasPageCloseHandler(QWidget* page) const { return bool(pages.value(page).close); }
bool EditorSplitController::requestPageClose(QWidget* page)
{
    const auto callback=pages.value(page).close;
    return callback && callback();
}
void EditorSplitController::setRegionRevealHandler(const QString& id,std::function<void()> reveal)
{
    if (regionHosts.contains(id)) regionHosts[id].reveal=std::move(reveal);
}
void EditorSplitController::revealGroup(QTabWidget* group)
{
    if (restoringPageLayout) return;
    if (isFloatingGroup(group)) { group->window()->show(); return; }
    const auto reveal=regionHosts.value(regionForGroup(group)).reveal;
    if (reveal) reveal();
}
bool EditorSplitController::activatePage(QWidget* page,bool focus)
{
    auto* group=groupForPage(page); if (!group) return false;
    revealGroup(group);
    { const QSignalBlocker block(group); group->setCurrentWidget(page); }
    if (currentGroup == group) emit pageActivated(page);
    else setActiveGroup(group);
    if (focus) page->setFocus(Qt::OtherFocusReason);
    return true;
}
void EditorSplitController::updateRegionRoot(const QString& id,QWidget* root)
{
    regionHosts[id].root=root;
    if (id==QStringLiteral("main")) rootWidget=root;
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
    regionHosts[regionForGroup(group)].current = group;
    emit activeGroupChanged(group);
    emit pageActivated(group->currentWidget());
}

QList<QTabWidget*> EditorSplitController::groups() const
{
    QList<QTabWidget*> result;
    const std::function<void(QWidget*)> visit=[&](QWidget* widget) {
        if (auto* group=qobject_cast<QTabWidget*>(widget); group && tabGroups.contains(group)) result.append(group);
        else if (auto* splitter=qobject_cast<QSplitter*>(widget))
            for (int i=0;i<splitter->count();++i) visit(splitter->widget(i));
    };
    for (const auto& id : {QStringLiteral("main"),QStringLiteral("left"),QStringLiteral("right"),QStringLiteral("bottom")})
        visit(regionRoot(id));
    for (const auto& group : tabGroups) if (group && !result.contains(group)) result.append(group);
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
        || direction == EditorSplitDirection::Center || isFloatingGroup(source)) {
        return nullptr;
    }
    const QString regionId = regionForGroup(source);
    const auto region = regionHosts.value(regionId);
    if (!region.host) return nullptr;
    if (groupMaximized) restoreGroupLayout();

    QTabWidget* created = createGroup(regionId);
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
        connect(nested,&QSplitter::splitterMoved,this,[this,nested] { emit splitterResized(nested); emit layoutChanged(); });
        if (parentSplitter) {
            const int sourceIndex =
                parentSplitter->indexOf(source);
            parentSplitter->replaceWidget(sourceIndex, nested);
        } else if (region.host->layout()) {
            delete region.host->layout()->replaceWidget(source, nested);
            updateRegionRoot(regionId, nested);
        }
        if (before) {
            nested->addWidget(created);
            nested->addWidget(source);
        } else {
            nested->addWidget(source);
            nested->addWidget(created);
        }
    }
    bindGroupDropTargets(source);
    bindGroupDropTargets(created);
    // Reparenting a visible QWidget hides it. Both sides of a new split must
    // be shown explicitly before QSplitter allocates their shared geometry.
    if (auto* splitter = qobject_cast<QSplitter*>(created->parentWidget())) splitter->show();
    source->show(); created->show();
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
    if (source==destination) {
        if (destinationIndex>=0) source->tabBar()->moveTab(sourceIndex,qBound(0,destinationIndex,source->count()-1));
        activatePage(page,false); emit layoutChanged(); return true;
    }
#ifdef ZEROSLACK_ENABLE_ELA
    if (auto* elaSource = qobject_cast<ElaTabWidget*>(source)) {
        if (elaSource->hasHostedTabs()) {
            const bool moved=elaSource->transferHostedTab(page,qobject_cast<ElaTabWidget*>(destination),destinationIndex);
            if (moved && source==destination) { activatePage(page,false); emit layoutChanged(); }
            return moved;
        }
    }
#endif

    QWidget* takenPage = nullptr;
    const PagePresentation presentation =
        takePagePresentation(source, sourceIndex, &takenPage);
    if (!takenPage)
        return false;
    QPointer<QWidget> pageGuard(takenPage), focused(QApplication::focusWidget());
    QPointer<QTabWidget> sourceGuard(source), targetGuard(destination);
    QSignalBlocker sourceBlock(source), targetBlock(destination);
    source->removeTab(sourceIndex);
    if (!sourceGuard) sourceBlock.dismiss();
    if (!targetGuard) targetBlock.dismiss();
    if (!pageGuard || !sourceGuard || !targetGuard) {
        if (sourceGuard && pageGuard) insertPage(sourceGuard,pageGuard,presentation,sourceIndex);
        if (focused) focused->setFocus(Qt::OtherFocusReason);
        return false;
    }
    insertPage(destination,takenPage,presentation,destinationIndex);
    const bool moved=targetGuard && pageGuard && targetGuard->indexOf(pageGuard)>=0;
    if (!sourceGuard) sourceBlock.dismiss();
    if (!targetGuard) targetBlock.dismiss();
    if (!moved) {
        if (sourceGuard && pageGuard) insertPage(sourceGuard,pageGuard,presentation,sourceIndex);
        if (focused) focused->setFocus(Qt::OtherFocusReason);
        return false;
    }
    sourceBlock.unblock(); targetBlock.unblock();
    activatePage(page,false);
    if (focused && (focused==page || page->isAncestorOf(focused))) focused->setFocus(Qt::OtherFocusReason);
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

bool EditorSplitController::retainGroup(QTabWidget* group)
{
    if (!group || !tabGroups.contains(group)) return false;
    ++retainedGroups[group];
    return true;
}

void EditorSplitController::releaseGroup(QTabWidget* group)
{
    auto it=retainedGroups.find(group);
    if (it==retainedGroups.end()) return;
    if (--it.value()==0) retainedGroups.erase(it);
    removeEmptyGroups();
}

void EditorSplitController::removeEmptyGroups()
{
    if (restoringPageLayout || tabGroups.size() <= 1)
        return;
    const QList<QPointer<QTabWidget>> snapshot = tabGroups;
    for (const QPointer<QTabWidget>& groupPointer : snapshot) {
        QTabWidget* group = groupPointer;
        if (!group || group->count() != 0 || retainedGroups.contains(group)
            || regionHosts.value(regionForGroup(group)).anchor == group
            || tabGroups.size() <= 1) {
            continue;
        }
#ifdef ZEROSLACK_ENABLE_ELA
        if (auto* ela = qobject_cast<ElaTabWidget*>(group); ela && ela->isHostedTabDragging())
            continue;
#endif
        QTabWidget* replacement = nullptr;
        for (QTabWidget* candidate : groups()) {
            if (candidate != group && !isFloatingGroup(candidate) && regionForGroup(candidate)==regionForGroup(group)) {
                replacement = candidate;
                break;
            }
        }
        // Keep one docked group as the return/drop target, even when every
        // document currently lives in an Ela floating window.
        if (!isFloatingGroup(group) && !replacement)
            continue;
        QSplitter* parentSplitter =
            qobject_cast<QSplitter*>(group->parentWidget());
        tabGroups.removeAll(group);
        groupRegions.remove(group);
        if (firstGroup == group)
            firstGroup = replacement;
        if (currentGroup == group)
            setActiveGroup(replacement ? replacement : firstGroup.data());
#ifdef ZEROSLACK_ENABLE_ELA
        if (auto* ela = qobject_cast<ElaTabWidget*>(group); ela && ela->isFloatingTabWidget()) {
            ela->disposeEmptyFloatingWindow();
        } else
#endif
        {
            group->setParent(nullptr);
            group->deleteLater();
        }
        collapseRedundantSplitter(parentSplitter);
        emit groupRemoved();
    }
}

QTabWidget* EditorSplitController::floatPage(QWidget* page, const QPoint& globalPosition)
{
#ifdef ZEROSLACK_ENABLE_ELA
    if (auto* ela = qobject_cast<ElaTabWidget*>(groupForPage(page)))
        return ela->floatHostedTab(page, globalPosition);
#endif
    return nullptr;
}

void EditorSplitController::syncFloatingVisibility()
{
#ifdef ZEROSLACK_ENABLE_ELA
    for (QTabWidget* group : groups()) {
        if (auto* ela = qobject_cast<ElaTabWidget*>(group))
            ela->syncFloatingVisibility();
    }
#endif
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
    if (active && isFloatingGroup(active)) {
        active->window()->isMaximized() ? active->window()->showNormal() : active->window()->showMaximized();
        return;
    }
    if (!active || tabGroups.size() < 2)
        return;

    savedSplitterSizes.clear();
    savedGroupVisibility.clear();
    maximizedRegion = regionForGroup(active);
    collectSplitterSizes(regionRoot(maximizedRegion));
    for (QTabWidget* group : groups()) {
        if (isFloatingGroup(group) || regionForGroup(group) != maximizedRegion)
            continue;
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
        if (isFloatingGroup(group) || !savedGroupVisibility.contains(group))
            continue;
        group->setVisible(
            savedGroupVisibility.value(group, true));
    }
    restoreSplitterSizes(regionRoot(maximizedRegion));
    maximizedRegion.clear();
    savedSplitterSizes.clear();
    savedGroupVisibility.clear();
    groupMaximized = false;
    emit layoutChanged();
}

void EditorSplitController::equalizeSplitSizes()
{
    equalizeSplitter(regionRoot(regionForGroup(activeGroup())));
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
    if (event->type()==QEvent::LayoutRequest)
        updateGroupMinimumWidth(groupForObject(watched));
    if (event->type()==QEvent::FocusIn || event->type()==QEvent::MouseButtonPress) {
        if (auto* group=groupForObject(watched)) {
            setActiveGroup(group);
        }
    }
    QTabBar* bar=qobject_cast<QTabBar*>(watched);
    if (bar) {
        auto* owner=groupForObject(bar);
        if (!owner || owner->tabBar()!=bar) return QObject::eventFilter(watched,event);
    }
#ifdef ZEROSLACK_ENABLE_ELA
    if (auto* ela = qobject_cast<ElaTabWidget*>(groupForObject(watched)); ela && ela->hasHostedTabs()) {
        if (auto* bar = qobject_cast<QTabBar*>(watched); bar && event->type() == QEvent::ContextMenu) {
            showTabContextMenu(bar, static_cast<QContextMenuEvent*>(event)->pos());
            return true;
        }
        return false;
    }
#endif
    if (event->type() == QEvent::ChildAdded
        && qobject_cast<QStackedWidget*>(watched) && groupForObject(watched)) {
        auto* childEvent = static_cast<QChildEvent*>(event);
        if (auto* childWidget =
                qobject_cast<QWidget*>(childEvent->child())) {
            bindDropTarget(childWidget);
        }
    }

    if (bar && event->type() == QEvent::MouseButtonPress) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) {
            pressedBar = bar;
            dragStartPosition = mouse->position().toPoint();
            pressedTabIndex = bar->tabAt(dragStartPosition);
            QTabWidget* group = groupForObject(bar);
            pressedPage = group && pressedTabIndex >= 0
                    && pressedTabIndex < group->count()
                ? group->widget(pressedTabIndex)
                : nullptr;
        }
    } else if (bar && event->type() == QEvent::MouseMove) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (startTabDrag(bar, mouse))
            return true;
    } else if (bar
               && event->type() == QEvent::MouseButtonRelease) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) {
            pressedBar = nullptr;
            pressedPage = nullptr;
            pressedTabIndex = -1;
        }
    } else if (bar && event->type() == QEvent::ContextMenu) {
        auto* context = static_cast<QContextMenuEvent*>(event);
        showTabContextMenu(bar, context->pos());
        return true;
    }

    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape)
            clearDropPreview();
    }

    if (event->type() == QEvent::DragEnter
        || event->type() == QEvent::DragMove) {
        auto* drag = static_cast<QDragMoveEvent*>(event);
        if (drag->mimeData()->hasFormat(
                QString::fromLatin1(kEditorTabMime))) {
            const QString viewId = QString::fromUtf8(
                drag->mimeData()->data(
                    QString::fromLatin1(kEditorTabMime)));
            QTabWidget* target = dropTargetForEvent(
                watched, drag->position().toPoint());
            const QPoint targetPosition = targetPositionForEvent(
                watched,
                target,
                drag->position().toPoint());
            if (updateDropPreview(
                    viewId, target, targetPosition)) {
                drag->setDropAction(Qt::MoveAction);
                drag->accept();
            } else {
                drag->ignore();
            }
            return true;
        }
    } else if (event->type() == QEvent::DragLeave) {
        if (dropPreview && dropPreview->isVisible()) {
            clearDropPreview();
            return true;
        }
    } else if (event->type() == QEvent::Drop) {
        auto* drop = static_cast<QDropEvent*>(event);
        if (!drop->mimeData()->hasFormat(
                QString::fromLatin1(kEditorTabMime))) {
            return QObject::eventFilter(watched, event);
        }
        const QString viewId = QString::fromUtf8(
            drop->mimeData()->data(
                QString::fromLatin1(kEditorTabMime)));
        QTabWidget* target = dropTargetForEvent(
            watched, drop->position().toPoint());
        const QPoint targetPosition = targetPositionForEvent(
            watched,
            target,
            drop->position().toPoint());
        const bool hasPreview = updateDropPreview(
            viewId, target, targetPosition);
        const EditorSplitDirection direction =
            hasPreview && dropPreview
            ? dropPreview->direction()
            : dropDirection(target, targetPosition);
        clearDropPreview();
        if (hasPreview
            && handleTabDrop(viewId, target, direction)) {
            drop->setDropAction(Qt::MoveAction);
            drop->accept();
            return true;
        }
        drop->ignore();
        return true;
    }
    return QObject::eventFilter(watched, event);
}

QTabWidget* EditorSplitController::createGroup(const QString& regionId)
{
    auto* group = UiControls::editorTabWidget(regionHosts.value(regionId).host);
    groupRegions.insert(group, regionId);
    configureGroup(group);
    tabGroups.append(group);
    emit groupCreated(group);
    return group;
}

void EditorSplitController::configureGroup(QTabWidget* group)
{
    if (!group)
        return;
    if (group->property("pageGroupId").toString().isEmpty())
        group->setProperty("pageGroupId", QUuid::createUuid().toString(QUuid::WithoutBraces));
    connect(group,&QObject::destroyed,this,[this,group] {
        groupRegions.remove(group); retainedGroups.remove(group); tabGroups.removeAll(group); tabGroups.removeAll(nullptr);
    });
#ifdef ZEROSLACK_ENABLE_ELA
    if (auto* ela = qobject_cast<ElaTabWidget*>(group)) {
        ela->setHostedTabs(this,
            [this](ElaTabWidget* target, ElaTabWidget::DropArea area) -> ElaTabWidget* {
                EditorSplitDirection direction = EditorSplitDirection::Center;
                switch (area) {
                case ElaTabWidget::DropArea::Left: direction = EditorSplitDirection::Left; break;
                case ElaTabWidget::DropArea::Right: direction = EditorSplitDirection::Right; break;
                case ElaTabWidget::DropArea::Top: direction = EditorSplitDirection::Above; break;
                case ElaTabWidget::DropArea::Bottom: direction = EditorSplitDirection::Below; break;
                case ElaTabWidget::DropArea::Center: return target;
                }
                return qobject_cast<ElaTabWidget*>(createSplit(target, direction));
            },
            [this]() -> ElaTabWidget* {
                for (QTabWidget* candidate : groups()) {
                    if (!isFloatingGroup(candidate))
                        return qobject_cast<ElaTabWidget*>(candidate);
                }
                return nullptr;
            });
        if (!group->count()) ela->setHostedTabBar(qobject_cast<ElaTabBar*>(UiControls::tabBar(group)));
        connect(ela, &ElaTabWidget::floatingTabWidgetCreated, this, [this,group](ElaTabWidget* floating) {
            groupRegions.insert(floating, regionForGroup(group));
            tabGroups.append(floating);
            configureGroup(floating);
            if (splitHost) {
                for (QAction* action : splitHost->window()->findChildren<QAction*>()) {
                    if (action->shortcutContext() == Qt::WindowShortcut && !action->shortcuts().isEmpty())
                        floating->window()->addAction(action);
                }
            }
            emit groupCreated(floating);
        });
        connect(ela, &ElaTabWidget::hostedTabMoved, this,
            [this](QWidget* page, ElaTabWidget* source, ElaTabWidget* target) {
                bindGroupDropTargets(target);
                registerPage(page);
                activatePage(page,false);
                emit pageMoved(page, source, target);
                removeEmptyGroups();
                emit layoutChanged();
            });
        connect(ela, &ElaTabWidget::hostedTabDragStarted, this, &EditorSplitController::tabDragStarted);
        connect(ela, &ElaTabWidget::hostedTabDragFinished, this, [this] {
            emit tabDragFinished();
            removeEmptyGroups();
            emit layoutChanged();
        });
    }
#endif
    if (group->objectName().isEmpty()) group->setObjectName(
        QStringLiteral("editorTabGroup%1").arg(tabGroups.size()+1));
    group->setDocumentMode(true);
    // Hidden tool pages must not dictate the width of another active page.
    group->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    group->setMinimumWidth(120);
    group->setTabsClosable(true);
    group->setMovable(true);
    group->setElideMode(Qt::ElideMiddle);
    group->tabBar()->setProperty("editorFileTabs", true);
    const bool leftLabels = regionForGroup(group) == QStringLiteral("left") && !isFloatingGroup(group);
    group->tabBar()->setProperty("currentLabelTabs", leftLabels);
    if (leftLabels) group->tabBar()->setFocusPolicy(Qt::StrongFocus);
    group->tabBar()->setExpanding(false);
    group->tabBar()->setUsesScrollButtons(true);
    if (!group->tabBar()->property("zeroslackElaControl").toBool())
        group->tabBar()->setStyleSheet(QStringLiteral("QTabBar::tab { min-width: 108px; }"));
    bindGroupDropTargets(group);
    connect(group,
            &QTabWidget::currentChanged,
            this,
            [this, group](int index) {
                if (index >= 0) {
                    bindDropTarget(group->widget(index));
                    registerPage(group->widget(index));
                }
                updateGroupMinimumWidth(group);
                if (activeGroup() == group)
                    emit pageActivated(index >= 0 ? group->widget(index) : nullptr);
            });
    connect(group,
            &QTabWidget::tabCloseRequested,
            this,
            [this, group](int index) {
                emit tabCloseRequested(group, index);
            });
}

void EditorSplitController::updateGroupMinimumWidth(QTabWidget* group)
{
    if (!group) return;
    // Ignore hidden pages' size hints, while retaining the visible page's
    // explicit minimum so a scrollable region can expose it without clipping.
    const auto* page=group->currentWidget();
    const int chrome=2*group->style()->pixelMetric(QStyle::PM_DefaultFrameWidth,nullptr,group);
    group->setMinimumWidth(qMax(120,page ? page->minimumWidth()+chrome : 0));
}

void EditorSplitController::bindGroupDropTargets(
    QTabWidget* group)
{
    if (!group)
        return;
    bindDropTarget(group);
    if (QTabBar* bar = group->tabBar())
        bindDropTarget(bar);
    if (auto* stack =
            group->findChild<QStackedWidget*>()) {
        bindDropTarget(stack);
    }
    for (int index = 0; index < group->count(); ++index)
        bindDropTarget(group->widget(index));
}

void EditorSplitController::bindDropTarget(QWidget* target)
{
    if (!target)
        return;
    target->setAcceptDrops(true);
    target->installEventFilter(this);
    if (auto* scrollArea =
            qobject_cast<QAbstractScrollArea*>(target)) {
        QWidget* viewport = scrollArea->viewport();
        if (viewport) {
            viewport->setAcceptDrops(true);
            viewport->installEventFilter(this);
        }
    }
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
    return pageId(page);
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
    presentation.whatsThis = group->tabWhatsThis(index);
    presentation.color = group->tabBar()->tabTextColor(index);
    presentation.enabled = group->isTabEnabled(index);
    presentation.visible = group->isTabVisible(index);
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
    group->setTabWhatsThis(inserted, presentation.whatsThis);
    group->tabBar()->setTabTextColor(inserted, presentation.color);
    group->setTabEnabled(inserted, presentation.enabled);
    group->setTabVisible(inserted, presentation.visible);
    if (presentation.visible) group->setCurrentIndex(inserted);
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
    } else {
        for (const auto& id : regions()) {
            const auto region = regionHosts.value(id);
            if (region.root != splitter || !region.host || !region.host->layout()) continue;
            delete region.host->layout()->replaceWidget(splitter, survivor);
            survivor->setParent(region.host); survivor->show(); updateRegionRoot(id,survivor); break;
        }
    }
    splitter->setParent(nullptr);
    splitter->deleteLater();
    collapseRedundantSplitter(parentSplitter);
}

QTabWidget* EditorSplitController::fallbackMergeDestination(
    QTabWidget* source) const
{
    for (QTabWidget* group : groups()) {
        if (group != source && regionForGroup(group) == regionForGroup(source))
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
    const int extent=splitter->orientation()==Qt::Horizontal ? splitter->width() : splitter->height();
    splitter->setSizes(QList<int>(splitter->count(),qMax(100,extent/qMax(1,splitter->count()))));
    for (int index = 0; index < splitter->count(); ++index)
        equalizeSplitter(splitter->widget(index));
}

EditorSplitDirection EditorSplitController::dropDirection(
    QTabWidget* target,
    const QPoint& position) const
{
    if (!target)
        return EditorSplitDirection::Center;
    return EditorDropPreviewOverlay::directionAt(
        target->rect(), position);
}

EditorDropPreviewOverlay*
EditorSplitController::ensureDropPreview()
{
    if (!splitHost)
        return nullptr;
    if (!dropPreview) {
        dropPreview = new EditorDropPreviewOverlay(splitHost);
    }
    return dropPreview;
}

QTabWidget* EditorSplitController::dropTargetForEvent(
    QObject* watched,
    const QPoint& watchedPosition) const
{
    if (QTabWidget* direct = groupForObject(watched))
        return direct;

    QWidget* watchedWidget = qobject_cast<QWidget*>(watched);
    if (watchedWidget) {
        const QPoint globalPosition =
            watchedWidget->mapToGlobal(watchedPosition);
        for (QTabWidget* group : groups()) {
            if (!group->isVisible())
                continue;
            const QRect globalRect(
                group->mapToGlobal(QPoint(0, 0)),
                group->size());
            if (globalRect.contains(globalPosition))
                return group;
        }
    }
    return activeGroup();
}

QPoint EditorSplitController::targetPositionForEvent(
    QObject* watched,
    QTabWidget* target,
    const QPoint& watchedPosition) const
{
    if (!target)
        return {};
    QWidget* watchedWidget = qobject_cast<QWidget*>(watched);
    if (!watchedWidget)
        return {};
    return target->mapFromGlobal(
        watchedWidget->mapToGlobal(watchedPosition));
}

bool EditorSplitController::updateDropPreview(
    const QString& viewId,
    QTabWidget* target,
    const QPoint& targetPosition)
{
    QWidget* page = pageForViewId(viewId);
    if (!page || !target || !tabGroups.contains(target)) {
        clearDropPreview();
        return false;
    }

    if (previewSourcePage != page) {
        QObject::disconnect(previewSourceDestroyedConnection);
        previewSourcePage = page;
        previewSourceDestroyedConnection = connect(
            page,
            &QObject::destroyed,
            this,
            [this]() {
                clearDropPreview();
            });
    }

    EditorDropPreviewOverlay* overlay = ensureDropPreview();
    if (!overlay) {
        clearDropPreview();
        return false;
    }
    overlay->showPreview(target, targetPosition);
    return true;
}

void EditorSplitController::clearDropPreview()
{
    QObject::disconnect(previewSourceDestroyedConnection);
    previewSourceDestroyedConnection = {};
    previewSourcePage.clear();
    if (dropPreview)
        dropPreview->clearPreview();
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
    QWidget* page = pressedPage;
    const int currentTabIndex =
        group && page ? group->indexOf(page) : -1;
    const QString viewId = viewIdForPage(page);
    if (viewId.isEmpty() || currentTabIndex < 0)
        return false;

    auto* mime = new QMimeData;
    mime->setData(
        QString::fromLatin1(kEditorTabMime),
        viewId.toUtf8());
    auto* drag = new QDrag(bar);
    drag->setMimeData(mime);
    const QPixmap pixmap =
        EditorDropPreviewOverlay::tabDragPixmap(
            bar, currentTabIndex);
    if (!pixmap.isNull()) {
        drag->setPixmap(pixmap);
        const QRect tabRect = bar->tabRect(currentTabIndex);
        const QSize logicalSize(
            qRound(pixmap.width()
                   / pixmap.devicePixelRatio()),
            qRound(pixmap.height()
                   / pixmap.devicePixelRatio()));
        const QPoint pressedOffset =
            dragStartPosition - tabRect.topLeft();
        drag->setHotSpot(QPoint(
            qBound(0, pressedOffset.x(), logicalSize.width() - 1),
            qBound(0, pressedOffset.y(), logicalSize.height() - 1)));
    }
    pressedBar = nullptr;
    pressedPage = nullptr;
    pressedTabIndex = -1;
    drag->exec(Qt::MoveAction, Qt::MoveAction);
    clearDropPreview();
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
        {ActionIds::ViewTemporaryEditorOpen, true, false, false},
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
                || groups(regionForGroup(group)).size() > 1);
        if (page && page->property("toolPage").toBool()
            && (item.actionId==QLatin1String(ActionIds::ViewEditorTabDuplicate)
                || item.actionId==QLatin1String(ActionIds::ViewEditorTabToggleLocked)
                || item.actionId==QLatin1String(ActionIds::ViewTemporaryEditorOpen))) item.enabled=false;
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

    std::unique_ptr<QMenu> menuOwner(UiControls::menu(bar));
    QMenu& menu = *menuOwner;
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
