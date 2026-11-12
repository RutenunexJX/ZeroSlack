#include "uicontrols.h"
#include "roundedicons.h"
#include "contextdockhost.h"
#include "editorsplitcontroller.h"
#include <QTabWidget>
#include <QTabBar>
#include <QAction>
#include <QSignalBlocker>
#include <QDynamicPropertyChangeEvent>
#include "contextfloatingwindow.h"
#include "contextdocktransition.h"
#include "applicationthememanager.h"
#ifdef ZEROSLACK_ENABLE_ELA
#include "ElaDragHandle.h"
#endif
#include <QApplication>
#include <QEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QScrollArea>
#include <QStyle>
#include <QAbstractButton>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTimer>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QMainWindow>
#include <QPainter>
#include <QSplitter>
#include <QScrollBar>

namespace {
constexpr int resizeHeight = 5;
struct SectionExtent { int minimum; int requested; };
QList<int> distributeExtents(int extent,const QList<SectionExtent>& items)
{
    QList<int> lengths,flexible; qint64 weights=0; int remaining=extent,minimumTotal=0;
    for (int i=0;i<items.size();++i) {
        lengths.append(items[i].minimum); minimumTotal+=items[i].minimum;
        flexible.append(i); weights+=qMax(1,items[i].requested);
    }
    if (minimumTotal>=extent) return lengths;
    while (!flexible.isEmpty()) {
        QList<int> constrained;
        for (int i : flexible)
            if (qint64(remaining)*qMax(1,items[i].requested)<qint64(items[i].minimum)*weights) constrained.append(i);
        if (constrained.isEmpty()) {
            qint64 accumulated=0; int allocated=0;
            for (int i : flexible) {
                accumulated+=qMax(1,items[i].requested);
                const int next=qint64(remaining)*accumulated/weights;
                lengths[i]=next-allocated; allocated=next;
            }
            break;
        }
        for (int i : constrained) { remaining-=items[i].minimum; weights-=qMax(1,items[i].requested); flexible.removeOne(i); }
    }
    return lengths;
}
QString displayTitle(const ContextResource& resource, QWidget* view)
{
    const QString title = view ? view->property("contextDisplayTitle").toString() : QString();
    if (!title.isEmpty()) return title;
    if (!resource.title.isEmpty()) return resource.title;
    return resource.uri.scheme() == "untitled" ? QStringLiteral("untitled") : resource.uri.fileName();
}
}
struct ContextDockHost::Section {
    QWidget* frame;
    QWidget* view;
    QPointer<QWidget> resize;
    QAction* fullView;
    bool detachable = false;
    int height = 0;
    int retainedHeight = 0;
    int width = 0;
};

ContextDockHost::ContextDockHost(QWidget* parent, EditorSplitController* sharedRegistry)
    : QWidget(parent), registry(sharedRegistry), ownsHosts(!sharedRegistry)
{
    setObjectName(QStringLiteral("contextDockHost"));
    setAcceptDrops(true);
    if (!registry) registry=new EditorSplitController(nullptr,this);
    rightScroll=new QScrollArea(this);
    rightScroll->setObjectName(QStringLiteral("contextGroupsScroll"));
    rightScroll->setFrameShape(QFrame::NoFrame);
    rightScroll->setWidgetResizable(true);
    rightScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rightRoot=new QWidget;
    rightRoot->installEventFilter(this);
    rightRoot->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    rightScroll->setWidget(rightRoot);
    auto* hostLayout=new QVBoxLayout(this);
    hostLayout->setContentsMargins(0,0,0,0); hostLayout->setSpacing(0);
    hostLayout->addWidget(rightScroll);
    registry->registerRegion(QStringLiteral("right"),rightRoot);
    rightRoot->layout()->setSizeConstraint(QLayout::SetNoConstraint);
    const auto createScrollableRegion=[this](const QString& region, bool horizontal) -> QWidget* {
        auto* scroll=new QScrollArea(this);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        if (horizontal) scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        else scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* content=new QWidget;
        content->setSizePolicy(horizontal ? QSizePolicy::Preferred : QSizePolicy::Ignored,
                               horizontal ? QSizePolicy::Ignored : QSizePolicy::Preferred);
        scroll->setWidget(content);
        registry->registerRegion(region,content);
        content->layout()->setSizeConstraint(QLayout::SetNoConstraint);
        content->installEventFilter(this);
        scroll->hide();
        return scroll;
    };
    if (auto* host=registry->regionHost(QStringLiteral("bottom"))) bottomRoot=host;
    else {
        bottomRoot=createScrollableRegion(QStringLiteral("bottom"),true);
        bottomRoot->setObjectName(QStringLiteral("contextBottomHost"));
    }
    if (auto* host=registry->regionHost(QStringLiteral("left"))) leftRoot=host;
    else {
        leftRoot=createScrollableRegion(QStringLiteral("left"),false);
        leftRoot->setObjectName(QStringLiteral("contextLeftHost"));
    }
    for (auto* host : {bottomRoot.data(),leftRoot.data()}) { host->setAcceptDrops(true); host->installEventFilter(this); }
    insertionMarker=new QWidget(this); insertionMarker->setObjectName(QStringLiteral("contextSectionInsertion"));
    insertionMarker->setAutoFillBackground(true);
    auto colors=palette(); colors.setColor(QPalette::Window,colors.color(QPalette::Highlight));
    insertionMarker->setPalette(colors); insertionMarker->hide();
    connect(registry,&EditorSplitController::pageActivated,this,[this](QWidget* page) {
        if (page && sections.contains(page->property("contextResourceKey").toString()))
            emit currentResourceChanged(resources.value(page->property("contextResourceKey").toString()));
    });
    connect(registry,&EditorSplitController::splitterResized,this,[this](QSplitter* splitter) {
        for (auto it=sections.begin();it!=sections.end();++it) {
            auto* group=groupForResource(it.key());
            if (!group || group->parentWidget()!=splitter) continue;
            if (splitter->orientation()==Qt::Horizontal) it.value()->width=group->width();
            else it.value()->height=group->height();
        }
    });
    connect(registry,&EditorSplitController::layoutChanged,this,[this] {
        arrangeSections(); emit resourceOrderChanged(); emit sectionLayoutChanged();
    });
    connect(&ApplicationThemeManager::instance(),&ApplicationThemeManager::animationsEnabledChanged,
        this,[this](bool enabled) { if (!enabled) settleMotion(); });
    if (ownsHosts) connect(registry,&EditorSplitController::tabCloseRequested,this,[this](QTabWidget* group,int index) {
        registry->requestPageClose(group->widget(index));
    });
}
ContextDockHost::~ContextDockHost()
{
    if (registry) disconnect(registry,nullptr,this,nullptr);
    if (bottomRoot) bottomRoot->removeEventFilter(this);
    if (leftRoot) leftRoot->removeEventFilter(this);
    for (auto* section : sections) {
        disconnect(section->frame,nullptr,this,nullptr);
        section->view->removeEventFilter(this);
    }
    qDeleteAll(sections); sections.clear();
    if (ownsHosts) { delete bottomRoot; delete leftRoot; }
}
EditorSplitController* ContextDockHost::pageRegistry() const { return registry; }
QString ContextDockHost::regionForArea(Qt::DockWidgetArea area)
{
    if (area==Qt::LeftDockWidgetArea) return QStringLiteral("left");
    if (area==Qt::BottomDockWidgetArea) return QStringLiteral("bottom");
    if (area==Qt::RightDockWidgetArea) return QStringLiteral("right");
    return QStringLiteral("main");
}
QTabWidget* ContextDockHost::groupForResource(const QString& key) const
{
    const auto* section=sections.value(key);
    return section && registry ? registry->groupForPage(section->frame) : nullptr;
}
int ContextDockHost::resourceCount() const { return sections.size(); }
int ContextDockHost::areaResourceCount(bool bottom) const
{ return areaResourceCount(bottom ? Qt::BottomDockWidgetArea : Qt::RightDockWidgetArea); }
int ContextDockHost::areaResourceCount(Qt::DockWidgetArea area) const
{
    int count = 0;
    for (const auto& key : sections.keys()) if (resourceArea(key) == area) ++count;
    return count;
}
QWidget* ContextDockHost::bottomWidget() const { return bottomRoot; }
QWidget* ContextDockHost::leftWidget() const { return leftRoot; }
bool ContextDockHost::isBottomResource(const QString& key) const
{ return resourceArea(key) == Qt::BottomDockWidgetArea; }
Qt::DockWidgetArea ContextDockHost::resourceArea(const QString& key) const
{
    if (!registry) return Qt::NoDockWidgetArea;
    const auto region=registry->regionForGroup(groupForResource(key));
    if (region==QStringLiteral("left")) return Qt::LeftDockWidgetArea;
    if (region==QStringLiteral("bottom")) return Qt::BottomDockWidgetArea;
    if (region==QStringLiteral("right")) return Qt::RightDockWidgetArea;
    return Qt::NoDockWidgetArea;
}



int ContextDockHost::sectionWidth(const QString& key) const
{ const auto* section = sections.value(key); return section ? section->width : 0; }
bool ContextDockHost::setSectionWidth(const QString& key, int width)
{
    auto* section = sections.value(key);
    if (!section) return false;
    const int bounded = width <= 0 ? 0 : qBound(180, width, 8192);
    if (section->width == bounded) return true;
    settleMotion();
    section->width = bounded;
    arrangeSections();
    emit sectionLayoutChanged();
    return true;
}
bool ContextDockHost::moveResourceToArea(const QString& key, bool bottom, int index)
{ return moveResourceToArea(key, bottom ? Qt::BottomDockWidgetArea : Qt::RightDockWidgetArea, index); }
bool ContextDockHost::moveResourceToArea(const QString& key, Qt::DockWidgetArea area, int index)
{
    auto* section=sections.value(key); if (!section) return false;
    settleMotion();
    auto* target=registry->regionGroup(regionForArea(area)); if (!target) return false;
    auto* source=groupForResource(key);
    if (source && registry->regionForGroup(source)==regionForArea(area)) return index<0 || moveResource(key,index);
    if (target->count()) {
        auto keys=resourceKeys();
        keys.removeAll(key);
        QTabWidget* neighbor=index>=0 && index<keys.size() ? groupForResource(keys[index]) : nullptr;
        if (!neighbor || registry->regionForGroup(neighbor)!=regionForArea(area)) neighbor=registry->groups(regionForArea(area)).last();
        const bool before=index>=0 && index<keys.size() && groupForResource(keys[index])==neighbor;
        target=registry->createSplit(neighbor,area==Qt::BottomDockWidgetArea
            ? (before ? EditorSplitDirection::Left : EditorSplitDirection::Right)
            : (before ? EditorSplitDirection::Above : EditorSplitDirection::Below));
    }
    return target && registry->movePage(section->frame,target);
}
QStringList ContextDockHost::resourceKeys() const
{
    if (!registry) return sections.keys();
    QStringList keys;
    for (auto* group : registry->groups())
        for (int i=0;i<group->count();++i) {
            const auto key=group->widget(i)->property("contextResourceKey").toString();
            if (sections.contains(key) && !keys.contains(key)) keys.append(key);
        }
    return keys;
}
bool ContextDockHost::containsResource(const QString& key) const { return sections.contains(key); }
ContextResource ContextDockHost::resourceAt(int index) const { return resources.value(resourceKeys().value(index)); }
ContextResource ContextDockHost::currentResource() const
{
    if (!registry) return {};
    auto* group=registry->activeGroup();
    auto resource=group && group->currentWidget()
        ? resources.value(group->currentWidget()->property("contextResourceKey").toString()) : ContextResource();
    if (resource.isValid()) return resource;
    group=registry->regionActiveGroup(QStringLiteral("right"));
    return group && group->currentWidget()
        ? resources.value(group->currentWidget()->property("contextResourceKey").toString()) : ContextResource();
}
QWidget* ContextDockHost::viewForResource(const QString& key) const
{ auto* section = sections.value(key); return section ? section->view : nullptr; }
QWidget* ContextDockHost::sectionWidget(const QString& key) const
{ auto* section = sections.value(key); return section ? section->frame : nullptr; }
bool ContextDockHost::addResource(const ContextResource& resource, QWidget* view, bool fullViewAvailable)
{
    const QString key = resource.stableKey();
    if (!resource.isValid() || key.isEmpty() || !view) return false;
    if (containsResource(key)) { activateResource(key); return false; }
    settleMotion();
    auto* section = new Section;
    section->frame = new QWidget;
    section->frame->setProperty("contextResourceKey",key);
    section->frame->setProperty("toolPage",true);
    section->frame->setObjectName(QStringLiteral("contextDockSection"));
    section->fullView = new QAction(tr("Open in main area"), section->frame);
    section->fullView->setObjectName(QStringLiteral("contextOpenInMain"));
    section->fullView->setEnabled(fullViewAvailable);
    section->fullView->setVisible(fullViewAvailable);
    section->frame->addAction(section->fullView);
    connect(section->fullView, &QAction::triggered, this, [this, key] {
        emit fullViewResourceRequested(resources.value(key));
    });
    view->setParent(section->frame);
    view->setProperty("contextResourceKey", key);
    view->installEventFilter(this);
    section->view = view;
    auto* column = new QVBoxLayout(section->frame);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    column->setSizeConstraint(QLayout::SetNoConstraint);
    column->addWidget(view, 1);
    sections.insert(key, section);
    // A shared area can belong to an earlier-created dock and be destroyed
    // before this host. Drop its records while the frame is still identifiable.
    connect(section->frame, &QObject::destroyed, this, [this, key](QObject* frame) {
        auto* orphan = sections.value(key);
        if (!orphan || orphan->frame != frame) return;
        sections.remove(key);
        resources.remove(key);
        delete orphan;
    });
    setSectionDetachable(key, false);
    resources.insert(key, resource);
    registry->registerPage(section->frame,QStringLiteral("context:")+key,QStringLiteral("context"),[this,key] {
        emit closeResourceRequested(key); return !containsResource(key);
    });
    auto* group=registry->regionGroup(QStringLiteral("right"));
    if (group->count()) {
        const auto groups=registry->groups(QStringLiteral("right"));
        group=registry->createSplit(groups.last(),EditorSplitDirection::Below);
    }
    { const QSignalBlocker block(group); group->addTab(section->frame,RoundedIcons::icon(RoundedIcons::Info),displayTitle(resource,view)); }
    refreshSectionStatus(key);
    section->frame->show();
    view->show();
    // Registering content must not open its dock during restoration or setup.
    // The workspace controller reveals the requested destination explicitly.
    group->setCurrentWidget(section->frame);
    registry->setActiveGroup(group);
    arrangeSections();
    emit currentResourceChanged(resource);
    return true;
}
bool ContextDockHost::updateResource(const ContextResource& resource)
{
    auto* section = sections.value(resource.stableKey());
    if (!resource.isValid() || !section) return false;
    resources.insert(resource.stableKey(), resource);
    refreshSectionStatus(resource.stableKey());
    return true;
}

void ContextDockHost::refreshSectionStatus(const QString& key)
{
    auto* section = sections.value(key);
    if (!section || !section->view) return;
    const QString text = section->view->property("contextStatusText").toString().trimmed();
    const QVariant fullView = section->view->property("contextFullViewActionVisible");
    section->fullView->setVisible(section->fullView->isEnabled()
        && (!fullView.isValid() || fullView.toBool()));
    if (auto* group=groupForResource(key)) {
        const auto title=displayTitle(resources.value(key),section->view);
        group->setTabText(group->indexOf(section->frame),title);
        QStringList details{title};
        if (!text.isEmpty()) details.append(text);
        const QString statusTip = section->view->property("contextStatusTooltip").toString();
        if (!statusTip.isEmpty() && statusTip != text) details.append(statusTip);
        if (!resources.value(key).uri.isEmpty()) details.append(resources.value(key).uri.toString());
        group->setTabToolTip(group->indexOf(section->frame), details.join(QLatin1Char('\n')));
    }
}
bool ContextDockHost::setFullViewAvailable(const QString& key, bool available)
{
    auto* section = sections.value(key);
    if (!section) return false;
    section->fullView->setEnabled(available);
    refreshSectionStatus(key);
    return true;
}
void ContextDockHost::focusSection(const QString& key)
{
    if (auto* section=sections.value(key)) registry->activatePage(section->frame,false);
}
bool ContextDockHost::activateResource(const QString& key)
{
    auto* section=sections.value(key); if (!section) return false;
    setSectionCollapsed(key,false);
    return registry->activatePage(section->frame);
}
QWidget* ContextDockHost::takeResource(const QString& key)
{
    settleMotion();
    auto* section=sections.take(key); if (!section) return nullptr;
    auto* view=section->view;
    view->removeEventFilter(this); view->hide(); view->setProperty("contextResourceKey",QVariant()); view->setParent(nullptr);
    resources.remove(key);
    disconnect(section->frame,nullptr,this,nullptr);
    if (registry) registry->takePage(section->frame);
    section->frame->hide(); section->frame->setParent(nullptr); section->frame->deleteLater();
    delete section;
    emit currentResourceChanged(currentResource());
    arrangeSections();
    return view;
}
bool ContextDockHost::removeResource(const QString& key)
{ auto* view = takeResource(key); if (!view) return false; view->deleteLater(); return true; }
int ContextDockHost::indexOfResource(const QString& key) const { return resourceKeys().indexOf(key.trimmed()); }
bool ContextDockHost::isSectionCollapsed(const QString& key) const
{ Q_UNUSED(key); return false; }
int ContextDockHost::sectionHeight(const QString& key) const
{ auto* section = sections.value(key); return section ? section->height : 0; }
bool ContextDockHost::setSectionCollapsed(const QString& key, bool collapsed, bool animate)
{
    // Compatibility with existing callers/layouts: tab pages always expose their contents.
    Q_UNUSED(collapsed);
    Q_UNUSED(animate);
    return sections.contains(key);
}

int ContextDockHost::minimumSectionHeight(const Section* section) const
{ return resizeHeight
    + qMax(48, qMax(section->view->minimumHeight(), section->view->minimumSizeHint().height())); }
bool ContextDockHost::setSectionHeight(const QString& key, int height)
{
    auto* section = sections.value(key);
    if (!section) return false;
    const int bounded = height <= 0 ? 0 : qBound(minimumSectionHeight(section), height, 8192);
    if (section->height == bounded) return true;
    settleMotion();
    for (auto* other : sections) other->retainedHeight = 0;
    section->height = bounded;
    arrangeSections();
    emit sectionLayoutChanged();
    return true;
}
bool ContextDockHost::moveResource(const QString& key,int index)
{
    const auto keys=resourceKeys(); const int old=keys.indexOf(key);
    if (old<0) return false;
    index=qBound(0,index,int(keys.size())-1); if (index==old) return true;
    auto* group=groupForResource(key); auto* target=groupForResource(keys[index]);
    if (!group || !target) return false;
    settleMotion();
    if (group==target) return registry->movePage(sections.value(key)->frame,target,index);
    auto* splitter=qobject_cast<QSplitter*>(target->parentWidget());
    if (group->count()==1 && splitter && group->parentWidget()==splitter) {
        splitter->insertWidget(splitter->indexOf(target),group);
        arrangeSections();
        emit resourceOrderChanged(); emit sectionLayoutChanged(); return true;
    }
    return registry->movePage(sections.value(key)->frame,target,target->indexOf(sections.value(keys[index])->frame));
}
void ContextDockHost::settleMotion()
{
    if (auto* host = qobject_cast<QMainWindow*>(window())) {
        if (auto* transfer = host->findChild<ContextDockTransition*>()) transfer->finish();
    }
}

void ContextDockHost::arrangeSections()
{
    if (arranging || !registry) return;
    arranging=true;
    QSet<QSplitter*> splitters;
    for (auto it=sections.begin();it!=sections.end();++it) {
        auto* section=it.value(); auto* group=groupForResource(it.key()); if (!group) continue;
        const int minimum=minimumSectionHeight(section);
        section->frame->setMinimumWidth(resourceArea(it.key())==Qt::BottomDockWidgetArea
            ? qMax(180,qMax(section->view->minimumWidth(), section->view->minimumSizeHint().width())) : 0);
        section->frame->setMinimumHeight(minimum);
        section->frame->setMaximumHeight(QWIDGETSIZE_MAX);
        group->setMaximumHeight(QWIDGETSIZE_MAX);
        if (auto* splitter=qobject_cast<QSplitter*>(group->parentWidget())) splitters.insert(splitter);
        section->view->show();
    }
    for (const auto& region : {QStringLiteral("right"),QStringLiteral("left"),QStringLiteral("bottom")}) {
        auto* host=registry->regionHost(region); auto* root=registry->regionRoot(region);
        if (!host || !root || !host->parentWidget()
            || !qobject_cast<QScrollArea*>(host->parentWidget()->parentWidget())) continue;
        if (region==QStringLiteral("bottom")) host->setMinimumWidth(root->minimumSizeHint().width());
        else host->setMinimumHeight(root->minimumSizeHint().height());
    }
    for (auto* splitter : splitters) {
        const bool horizontal=splitter->orientation()==Qt::Horizontal;
        QList<SectionExtent> extents; bool allResources=true;
        for (int i=0;i<splitter->count();++i) {
            auto* group=qobject_cast<QTabWidget*>(splitter->widget(i));
            auto* section=group && group->count()==1 ? sections.value(group->widget(0)->property("contextResourceKey").toString()) : nullptr;
            if (!section) { allResources=false; break; }
            const int chrome=horizontal ? 2*group->style()->pixelMetric(QStyle::PM_DefaultFrameWidth,nullptr,group)
                : group->minimumSizeHint().height()-qMax(section->frame->minimumHeight(),section->frame->minimumSizeHint().height());
            const int minimum=horizontal ? qMax(180,qMax(section->view->minimumWidth(), section->view->minimumSizeHint().width()))
                : minimumSectionHeight(section);
            const int requested=horizontal ? section->width : section->height;
            extents.append({minimum+qMax(0,chrome),requested>0 ? requested : 1});
        }
        if (allResources) {
            const int extent=horizontal ? splitter->width() : splitter->height();
            splitter->setSizes(distributeExtents(qMax(0,extent-(splitter->count()-1)*splitter->handleWidth()),extents));
        }
    }
    arranging=false;
}


QRect ContextDockHost::viewportGlobalRect(bool bottom) const
{ return viewportGlobalRect(bottom ? Qt::BottomDockWidgetArea : Qt::RightDockWidgetArea); }
QRect ContextDockHost::viewportGlobalRect(Qt::DockWidgetArea dockArea) const
{
    auto* host=registry->regionHost(regionForArea(dockArea));
    if (host && host->parentWidget())
        if (auto* scroll=qobject_cast<QScrollArea*>(host->parentWidget()->parentWidget())) host=scroll->viewport();
    return host && host->isVisible() ? QRect(host->mapToGlobal(QPoint()),host->size()) : QRect();
}
QRect ContextDockHost::projectedSectionRect(bool bottom,int index,const QRect& viewport,QWidget* incoming) const
{
    if (viewport.isEmpty() || !incoming) return {};
    QList<SectionExtent> extents; int insertion=0; QPoint offset; QSize chrome; int handleWidth=4,minimumChrome=0;
    int tabHeight=30;
    const auto keys=resourceKeys();
    for (int i=0;i<keys.size();++i) {
        if (isBottomResource(keys[i])!=bottom) continue;
        auto* section=sections.value(keys[i]); auto* group=groupForResource(keys[i]);
        if (!group) continue;
        tabHeight=group->tabBar()->height();
        if (extents.isEmpty()) {
            offset=section->frame->mapTo(group,QPoint()); chrome=group->size()-section->frame->size();
            minimumChrome=bottom ? 2*group->style()->pixelMetric(QStyle::PM_DefaultFrameWidth,nullptr,group)
                : group->minimumSizeHint().height()-qMax(section->frame->minimumHeight(),section->frame->minimumSizeHint().height());
        }
        if (auto* splitter=qobject_cast<QSplitter*>(group->parentWidget())) handleWidth=splitter->handleWidth();
        if (i<index) ++insertion;
        const int minimum=(bottom ? qMax(180,qMax(section->view->minimumWidth(), section->view->minimumSizeHint().width()))
            : minimumSectionHeight(section))+minimumChrome;
        const int request=bottom ? section->width : section->height;
        extents.append({minimum,request>0 ? request : 1});
    }
    const int incomingMinimum=bottom ? qMax(180,incoming->minimumWidth())+minimumChrome
        : resizeHeight+qMax(48,qMax(incoming->minimumHeight(),incoming->minimumSizeHint().height()))+qMax(tabHeight,minimumChrome);
    extents.insert(insertion,{incomingMinimum,1});
    const int extent=bottom ? viewport.width() : viewport.height();
    const auto lengths=distributeExtents(qMax(0,extent-(extents.size()-1)*handleWidth),extents);
    int position=insertion*handleWidth; for (int i=0;i<insertion;++i) position+=lengths[i];
    QSize viewportSize=viewport.size();
    int total=(extents.size()-1)*handleWidth;
    for (const auto& item:extents) total+=item.minimum;
    auto* host=registry->regionHost(bottom ? QStringLiteral("bottom") : QStringLiteral("right"));
    auto* scroll=host && host->parentWidget() ? qobject_cast<QScrollArea*>(host->parentWidget()->parentWidget()) : nullptr;
    if (scroll && total>extent) {
        auto* bar=bottom ? scroll->horizontalScrollBar() : scroll->verticalScrollBar();
        if (!bar->isVisible()) {
            const int size=scroll->style()->pixelMetric(QStyle::PM_ScrollBarExtent,nullptr,scroll);
            if (bottom) viewportSize.rheight()-=size; else viewportSize.rwidth()-=size;
        }
    }
    QRect group=bottom ? QRect(viewport.topLeft()+QPoint(position,0),QSize(lengths[insertion],viewportSize.height()))
                       : QRect(viewport.topLeft()+QPoint(0,position),QSize(viewportSize.width(),lengths[insertion]));
    return QRect(group.topLeft()+offset,group.size()-chrome);
}
void ContextDockHost::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    for (auto* section : sections) section->retainedHeight = 0;
    arrangeSections();
}
int ContextDockHost::insertionIndex(const QPoint& position) const
{
    const auto keys=resourceKeys(); const auto area=areaAt(position);
    for (int i=0;i<keys.size();++i) {
        auto* frame=sectionWidget(keys[i]);
        if (resourceArea(keys[i])==area && frame) {
            const QRect bounds(frame->mapToGlobal(QPoint()),frame->size());
            if (bounds.contains(position)) return i+((area==Qt::BottomDockWidgetArea ? position.x()>bounds.center().x() : position.y()>bounds.center().y()) ? 1 : 0);
        }
    }
    return keys.size();
}

bool ContextDockHost::containsDropPosition(const QPoint& position) const
{ return areaAt(position) != Qt::NoDockWidgetArea; }
Qt::DockWidgetArea ContextDockHost::areaAt(const QPoint& position) const
{
    for (const auto area : {Qt::BottomDockWidgetArea,Qt::LeftDockWidgetArea,Qt::RightDockWidgetArea})
        if (viewportGlobalRect(area).contains(position)) return area;
    return Qt::NoDockWidgetArea;
}

void ContextDockHost::showInsertion(const QPoint& position)
{
    const auto area=areaAt(position);
    if (area==Qt::NoDockWidgetArea) { insertionMarker->hide(); return; }
    auto* host=registry->regionHost(regionForArea(area));
    insertionMarker->setParent(host);
    insertionMarker->setGeometry(host->rect().adjusted(2,2,-2,-2)); insertionMarker->show(); insertionMarker->raise();
}
bool ContextDockHost::eventFilter(QObject* watched,QEvent* event)
{
    if (!registry) return QWidget::eventFilter(watched,event);
    const bool regionHost=watched==rightRoot || watched==registry->regionHost(QStringLiteral("left"))
        || watched==registry->regionHost(QStringLiteral("bottom"));
    if (event->type()==QEvent::LayoutRequest
        && (regionHost || !watched->property("contextResourceKey").toString().isEmpty())
        && !arrangementPending) {
        arrangementPending=true;
        QTimer::singleShot(0,this,[this] { arrangementPending=false; arrangeSections(); });
    }
    if (regionHost && event->type()==QEvent::Resize) arrangeSections();
    if (watched==bottomRoot || watched==leftRoot) {
        auto* target=qobject_cast<QWidget*>(watched);
        if (event->type()==QEvent::Resize) arrangeSections();
        if (event->type()==QEvent::DragEnter) {
            auto* drag=static_cast<QDragEnterEvent*>(event);
            if (validFloatingSource(drag->source(),drag->mimeData())) { drag->acceptProposedAction(); return true; }
        } else if (event->type()==QEvent::DragMove) {
            auto* drag=static_cast<QDragMoveEvent*>(event);
            if (validFloatingSource(drag->source(),drag->mimeData())) {
                showInsertion(target->mapToGlobal(drag->position().toPoint())); drag->acceptProposedAction(); return true;
            }
        } else if (event->type()==QEvent::Drop) {
            auto* drop=static_cast<QDropEvent*>(event);
            if (validFloatingSource(drop->source(),drop->mimeData())) {
                clearFloatingDropPreview();
                if (acceptFloatingDrop(qobject_cast<ContextFloatingWindow*>(drop->source()),
                    QString::fromUtf8(drop->mimeData()->data(resourceMimeType())),target->mapToGlobal(drop->position().toPoint()))) drop->acceptProposedAction();
                return true;
            }
        }
    }
    const auto key=watched->property("contextResourceKey").toString();
    if (auto* section=sections.value(key)) {
        if (watched==section->view && event->type()==QEvent::DynamicPropertyChange) refreshSectionStatus(key);
    }
    return QWidget::eventFilter(watched,event);
}

bool ContextDockHost::setSectionDetachable(const QString& key, bool detachable)
{
    auto* section = sections.value(key);
    if (!section) return false;
    section->detachable = detachable;
    section->frame->setProperty("hostedTabDetachable", detachable);
    return true;
}
bool ContextDockHost::validFloatingSource(QObject* source, const QMimeData* mime) const
{
    auto* floating = qobject_cast<ContextFloatingWindow*>(source);
    return floating && floating->parentWidget() && floating->parentWidget()->isAncestorOf(this) && floating->canDock()
        && mime && mime->hasFormat(resourceMimeType())
        && floating->resource().stableKey() == QString::fromUtf8(mime->data(resourceMimeType()));
}
bool ContextDockHost::acceptFloatingDrop(ContextFloatingWindow* source, const QString& key, const QPoint& globalPosition)
{
    QMimeData mime; mime.setData(resourceMimeType(), key.toUtf8());
    if (!validFloatingSource(source, &mime) || !containsDropPosition(globalPosition)) return false;
    emit floatingDropRequested(key, insertionIndex(globalPosition), areaAt(globalPosition));
    return containsResource(key) && !source->hasResource();
}
void ContextDockHost::previewFloatingDrop(ContextFloatingWindow* source, const QPoint& globalPosition)
{
    QMimeData mime;
    if (source) mime.setData(resourceMimeType(), source->resource().stableKey().toUtf8());
    if (validFloatingSource(source, &mime)) showInsertion(globalPosition);
    else clearFloatingDropPreview();
}
void ContextDockHost::clearFloatingDropPreview() { insertionMarker->hide(); }
void ContextDockHost::dragEnterEvent(QDragEnterEvent* event)
{
    if (validFloatingSource(event->source(), event->mimeData())) event->acceptProposedAction();
}
void ContextDockHost::dragMoveEvent(QDragMoveEvent* event)
{
    if (!validFloatingSource(event->source(), event->mimeData())) { event->ignore(); return; }
    showInsertion(mapToGlobal(event->position().toPoint()));
    event->acceptProposedAction();
}
void ContextDockHost::dragLeaveEvent(QDragLeaveEvent* event) { insertionMarker->hide(); event->accept(); }
void ContextDockHost::dropEvent(QDropEvent* event)
{
    insertionMarker->hide();
    if (validFloatingSource(event->source(), event->mimeData())
        && acceptFloatingDrop(qobject_cast<ContextFloatingWindow*>(event->source()),
            QString::fromUtf8(event->mimeData()->data(resourceMimeType())), mapToGlobal(event->position().toPoint()))) {
        event->setDropAction(Qt::MoveAction);
        event->accept();
    } else event->ignore();
}
