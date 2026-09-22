#include "uicontrols.h"
#include "roundedicons.h"
#include "contextdockhost.h"
#include "contextfloatingwindow.h"
#include "panelcompositor.h"
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

namespace {
constexpr int resizeHeight = 5;
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
    QWidget* header;
    QWidget* view;
    QWidget* resize;
    QLabel* title;
    QLabel* status;
    QToolButton* scope;
    QToolButton* toggle;
    QToolButton* fullView;
    QToolButton* fit;
    QToolButton* drag;
    bool detachable = false;
    bool collapsed = false;
    int height = 0;
    int retainedHeight = 0;
    int width = 0;
    bool bottom = false;
};

ContextDockHost::ContextDockHost(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("contextDockHost"));
    setAcceptDrops(true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    scroll = UiControls::scrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(false);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    stack = new QWidget;
    scroll->setWidget(stack);
    scroll->viewport()->installEventFilter(this);
    layout->addWidget(scroll);
    bottomRoot = new QWidget(this);
    bottomRoot->setObjectName(QStringLiteral("contextBottomHost"));
    bottomRoot->setAcceptDrops(true);
    bottomRoot->installEventFilter(this);
    auto* bottomLayout = new QVBoxLayout(bottomRoot);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomScroll = UiControls::scrollArea(bottomRoot);
    bottomScroll->setFrameShape(QFrame::NoFrame);
    bottomScroll->setWidgetResizable(false);
    bottomScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    bottomStack = new QWidget;
    bottomScroll->setWidget(bottomStack);
    bottomScroll->viewport()->installEventFilter(this);
    bottomLayout->addWidget(bottomScroll);
    bottomRoot->hide();
    insertionMarker = new QWidget(stack);
    insertionMarker->setObjectName(QStringLiteral("contextSectionInsertion"));
    insertionMarker->setAutoFillBackground(true);
    QPalette markerPalette = palette();
    markerPalette.setColor(QPalette::Window, palette().color(QPalette::Highlight));
    insertionMarker->setPalette(markerPalette);
    insertionMarker->hide();
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) {
        for (const QString& key : order) {
            auto* section = sections.value(key);
            if (now && (section->frame == now || section->frame->isAncestorOf(now))) {
                focusSection(key);
                break;
            }
        }
    });
}
ContextDockHost::~ContextDockHost()
{
    disconnect(qApp, nullptr, this, nullptr);
    if (bottomRoot) bottomRoot->removeEventFilter(this);
    if (scroll) scroll->viewport()->removeEventFilter(this);
    if (bottomScroll) bottomScroll->viewport()->removeEventFilter(this);
    if (auto* host = qobject_cast<QMainWindow*>(window())) {
        if (auto* compositor = host->findChild<PanelCompositor*>())
            for (auto* section : sections) compositor->settleFor(section->frame);
    }
    for (auto* section : sections) {
        for (QObject* target : {section->view, section->header, section->resize, static_cast<QWidget*>(section->drag)})
            target->removeEventFilter(this);
    }
    qDeleteAll(sections);
    sections.clear();
    delete bottomRoot;
}
int ContextDockHost::resourceCount() const { return order.size(); }
int ContextDockHost::areaResourceCount(bool bottom) const
{
    int count = 0;
    for (auto* section : sections) if (section->bottom == bottom) ++count;
    return count;
}
QWidget* ContextDockHost::bottomWidget() const { return bottomRoot; }
bool ContextDockHost::isBottomResource(const QString& key) const
{ const auto* section = sections.value(key); return section && section->bottom; }
int ContextDockHost::sectionWidth(const QString& key) const
{ const auto* section = sections.value(key); return section ? section->width : 0; }
bool ContextDockHost::setSectionWidth(const QString& key, int width)
{
    auto* section = sections.value(key);
    if (!section) return false;
    const int bounded = width <= 0 ? 0 : qBound(180, width, 8192);
    if (section->width == bounded) return true;
    section->width = bounded;
    arrangeSections();
    emit sectionLayoutChanged();
    return true;
}
bool ContextDockHost::moveResourceToArea(const QString& key, bool bottom, int index)
{
    auto* section = sections.value(key);
    if (!section) return false;
    settleMotion();
    section->bottom = bottom;
    section->frame->setParent(bottom ? bottomStack : stack);
    section->frame->layout()->setContentsMargins(0, 0, bottom ? resizeHeight : 0, bottom ? 0 : resizeHeight);
    section->resize->setMinimumSize(0, 0);
    section->resize->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    section->resize->setCursor(bottom ? Qt::SizeHorCursor : Qt::SizeVerCursor);
    if (index >= 0) {
        const int oldIndex = order.indexOf(key);
        if (index > oldIndex) --index;
        order.move(oldIndex, qBound(0, index, int(order.size()) - 1));
    }
    section->frame->show();
    arrangeSections();
    emit sectionLayoutChanged();
    emit resourceOrderChanged();
    return true;
}
QStringList ContextDockHost::resourceKeys() const { return order; }
bool ContextDockHost::containsResource(const QString& key) const { return indexOfResource(key) >= 0; }
ContextResource ContextDockHost::resourceAt(int index) const { return resources.value(order.value(index)); }
ContextResource ContextDockHost::currentResource() const { return resources.value(focusedKey); }
QWidget* ContextDockHost::viewForResource(const QString& key) const
{ auto* section = sections.value(key); return section ? section->view : nullptr; }
QWidget* ContextDockHost::sectionWidget(const QString& key) const
{ auto* section = sections.value(key); return section ? section->frame : nullptr; }
QLabel* ContextDockHost::sectionStatus(const QString& key) const
{
    auto* section = sections.value(key);
    return section ? section->status : nullptr;
}
QAbstractButton* ContextDockHost::sectionScope(const QString& key) const
{
    auto* section = sections.value(key);
    return section ? static_cast<QAbstractButton*>(section->scope) : nullptr;
}
QWidget* ContextDockHost::sectionHeader(const QString& key) const
{ auto* section = sections.value(key); return section ? section->header : nullptr; }

bool ContextDockHost::addResource(const ContextResource& resource, QWidget* view, bool fullViewAvailable)
{
    const QString key = resource.stableKey();
    if (!resource.isValid() || key.isEmpty() || !view) return false;
    if (containsResource(key)) { activateResource(key); return false; }
    settleMotion();
    auto* section = new Section;
    section->frame = new QWidget(stack);
    section->frame->setObjectName(QStringLiteral("contextDockSection"));
    section->header = new QWidget(section->frame);
    section->header->setObjectName(QStringLiteral("contextSectionHeader"));
    section->header->setToolTip(resource.uri.toString());
    section->header->setFocusPolicy(Qt::StrongFocus);
    section->header->setProperty("contextResourceKey", key);
    section->header->installEventFilter(this);
    auto* row = new QHBoxLayout(section->header);
    row->setContentsMargins(4, 0, 4, 0);
    row->setSpacing(2);
    section->toggle = UiControls::toolButton(section->header);
    section->toggle->setProperty("panelMotionToggle", true);
    section->toggle->setArrowType(Qt::DownArrow);
    section->toggle->setToolTip(tr("Collapse or expand section"));
    row->addWidget(section->toggle);
    section->drag = UiControls::toolButton(section->header);
    section->drag->setObjectName(QStringLiteral("contextSectionDrag"));
    section->drag->setIcon(style()->standardIcon(QStyle::SP_TitleBarNormalButton));
    section->drag->setCursor(Qt::OpenHandCursor);
    section->drag->setProperty("contextResourceKey", key);
    section->drag->installEventFilter(this);
    row->addWidget(section->drag);
    section->title = UiControls::label(displayTitle(resource, view), section->header);
    section->title->setObjectName(QStringLiteral("contextSectionTitle"));
    section->title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    section->title->setAttribute(Qt::WA_TransparentForMouseEvents);
    row->addWidget(section->title, 1);
    // Freshness belongs on the title bar so the body can stay content.
    // A view publishes it through the contextStatus* properties; sections
    // whose view publishes nothing keep the header as it was.
    section->status = UiControls::label(section->header);
    section->status->setObjectName(QStringLiteral("contextSectionStatus"));
    section->status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    section->status->setAttribute(Qt::WA_TransparentForMouseEvents);
    section->status->hide();
    row->addWidget(section->status);
    // Same property channel as the status chip, one step further: a view that
    // names its current scope gets a clickable chip for choosing another. The
    // host stays generic — it just invokes the view's own slot.
    section->scope = UiControls::toolButton(section->header);
    section->scope->setObjectName(QStringLiteral("contextSectionScope"));
    section->scope->setCursor(Qt::PointingHandCursor);
    section->scope->hide();
    connect(section->scope, &QToolButton::clicked, this, [this, key] {
        auto* target = sections.value(key);
        if (target && target->view)
            QMetaObject::invokeMethod(target->view, "requestScopePick");
    });
    row->addWidget(section->scope);
    section->fit = UiControls::toolButton(section->header);
    section->fit->setObjectName(QStringLiteral("contextSectionFit"));
    section->fit->setText(tr("Fit"));
    section->fit->setToolTip(tr("Fit diagram to view"));
    connect(section->fit, &QToolButton::clicked, this, [this, key] {
        auto* target = sections.value(key);
        if (target && target->view) QMetaObject::invokeMethod(target->view, "fitGraph");
    });
    row->addWidget(section->fit);
    section->fullView = UiControls::toolButton(section->header);
    section->fullView->setObjectName(QStringLiteral("contextDockFullView"));
    section->fullView->setIcon(RoundedIcons::icon(RoundedIcons::Expand));
    section->fullView->setToolTip(tr("Open in main area"));
    section->fullView->setVisible(fullViewAvailable);
    row->addWidget(section->fullView);
    auto* unpin = UiControls::toolButton(section->header);
    unpin->setObjectName(QStringLiteral("contextDockUnpin"));
    unpin->setIcon(RoundedIcons::icon(RoundedIcons::Pin));
    unpin->setToolTip(tr("Move to preview"));
    row->addWidget(unpin);
    auto* close = UiControls::toolButton(section->header);
    close->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    close->setToolTip(tr("Close section"));
    row->addWidget(close);
    connect(section->toggle, &QToolButton::clicked, this, [this, key] { setSectionCollapsed(key, !isSectionCollapsed(key)); });
    connect(unpin, &QToolButton::clicked, this, [this, key] { emit unpinResourceRequested(key); });
    connect(close, &QToolButton::clicked, this, [this, key] { emit closeResourceRequested(key); });
    connect(section->fullView, &QToolButton::clicked, this, [this, key] { emit fullViewResourceRequested(resources.value(key)); });
    view->setParent(section->frame);
    view->setProperty("contextResourceKey", key);
    view->installEventFilter(this);
    section->view = view;
    section->resize = new QWidget(section->frame);
    section->resize->setObjectName(QStringLiteral("contextSectionResize"));
    section->resize->setCursor(Qt::SizeVerCursor);
    section->resize->setProperty("contextResourceKey", key);
    section->resize->installEventFilter(this);
    auto* column = new QVBoxLayout(section->frame);
    column->setContentsMargins(0, 0, 0, resizeHeight);
    column->setSpacing(0);
    column->setSizeConstraint(QLayout::SetNoConstraint);
    section->header->setFixedHeight(row->sizeHint().height());
    column->addWidget(section->header);
    column->addWidget(view, 1);
    sections.insert(key, section);
#ifdef ZEROSLACK_ENABLE_ELA
    if (ApplicationThemeManager::instance().backend() == UiStyleBackend::Ela) {
        for (QWidget* handle : {section->header, static_cast<QWidget*>(section->drag)}) {
            auto* gesture = new ElaDragHandle(handle, this);
            connect(gesture, &ElaDragHandle::pressed, this, [this, key] { focusSection(key); });
            connect(gesture, &ElaDragHandle::moved, this, [this](const QPoint& position) {
                emit sectionDragStarted();
                showInsertion(position);
            });
            connect(gesture, &ElaDragHandle::cancelled, this, [this] {
                insertionMarker->hide();
                emit sectionDragFinished();
            });
            connect(gesture, &ElaDragHandle::released, this, [this, key](const QPoint& position, bool dragged) {
                insertionMarker->hide();
                if (!dragged || !sections.contains(key)) return;
                finishSectionDrag(key, position);
            });
        }
    }
#endif
    setSectionDetachable(key, false);
    resources.insert(key, resource);
    order.append(key);
    refreshSectionStatus(key);
    section->frame->show();
    section->header->show();
    view->show();
    section->resize->show();
    activateResource(key);
    return true;
}
bool ContextDockHost::updateResource(const ContextResource& resource)
{
    auto* section = sections.value(resource.stableKey());
    if (!resource.isValid() || !section) return false;
    resources.insert(resource.stableKey(), resource);
    section->title->setText(displayTitle(resource, section->view));
    section->header->setToolTip(resource.uri.toString());
    refreshSectionStatus(resource.stableKey());
    return true;
}

void ContextDockHost::refreshSectionStatus(const QString& key)
{
    auto* section = sections.value(key);
    if (!section || !section->view) return;
    const QString text = section->view->property("contextStatusText").toString().trimmed();
    section->status->setText(text);
    section->status->setToolTip(section->view->property("contextStatusTooltip").toString());
    section->status->setVisible(!text.isEmpty());
    const QString scope = section->view->property("contextScopeText").toString().trimmed();
    section->scope->setText(scope);
    section->scope->setToolTip(section->view->property("contextScopeTooltip").toString());
    section->scope->setVisible(!scope.isEmpty());
    section->fit->setVisible(section->view->property("contextFitAvailable").toBool());
    section->header->setFixedHeight(section->header->layout()->sizeHint().height());
}
bool ContextDockHost::setFullViewAvailable(const QString& key, bool available)
{
    auto* section = sections.value(key);
    if (!section) return false;
    section->fullView->setVisible(available);
    return true;
}
void ContextDockHost::focusSection(const QString& key)
{
    if (!sections.contains(key) || focusedKey == key) return;
    settleMotion();
    focusedKey = key;
    arrangeSections();
    emit currentResourceChanged(resources.value(key));
}
bool ContextDockHost::activateResource(const QString& key)
{
    if (!sections.contains(key)) return false;
    focusSection(key);
    setSectionCollapsed(key, false);
    (isBottomResource(key) ? bottomScroll : scroll)->ensureWidgetVisible(sections.value(key)->header);
    sections.value(key)->header->setFocus(Qt::OtherFocusReason);
    return true;
}
QWidget* ContextDockHost::takeResource(const QString& key)
{
    settleMotion();
    for (const QString& other : order) {
        auto* section = sections.value(other);
        if (other != key && !section->collapsed) section->retainedHeight = section->frame->height();
    }
    auto* section = sections.take(key);
    if (!section) return nullptr;
    QWidget* view = section->view;
    view->hide();
    view->setProperty("contextResourceKey", QVariant());
    view->setParent(nullptr);
    order.removeAll(key);
    resources.remove(key);
    section->frame->hide();
    section->frame->deleteLater();
    delete section;
    if (focusedKey == key) {
        focusedKey = order.value(0);
        emit currentResourceChanged(currentResource());
    }
    arrangeSections();
    return view;
}
bool ContextDockHost::removeResource(const QString& key)
{ auto* view = takeResource(key); if (!view) return false; view->deleteLater(); return true; }
int ContextDockHost::indexOfResource(const QString& key) const { return order.indexOf(key.trimmed()); }
bool ContextDockHost::isSectionCollapsed(const QString& key) const
{ auto* section = sections.value(key); return section && section->collapsed; }
int ContextDockHost::sectionHeight(const QString& key) const
{ auto* section = sections.value(key); return section ? section->height : 0; }
bool ContextDockHost::setSectionCollapsed(const QString& key, bool collapsed, bool animate)
{
    auto* section = sections.value(key);
    if (!section) return false;
    if (section->collapsed == collapsed) return true;
    const auto apply = [this, section, collapsed] {
        for (auto* other : sections) other->retainedHeight = 0;
        section->collapsed = collapsed;
        section->toggle->setArrowType(collapsed ? Qt::RightArrow : Qt::DownArrow);
        arrangeSections();
        if (!collapsed) (section->bottom ? bottomScroll : scroll)->ensureWidgetVisible(section->header);
    };
    auto* host = qobject_cast<QMainWindow*>(window());
    const bool enabled = animate && !section->bottom && isVisible() && host && host->isVisible()
        && ApplicationThemeManager::instance().backend() == UiStyleBackend::Ela;
    auto* compositor = host ? host->findChild<PanelCompositor*>() : nullptr;
    if (enabled && !compositor) compositor = PanelCompositor::forWindow(host);
    if (enabled && compositor->reverse(section->frame, !collapsed, apply)) {
        emit sectionLayoutChanged(); return true;
    }
    if (compositor && isVisible()) compositor->settle();
    if (!enabled) { apply(); emit sectionLayoutChanged(); return true; }
    const QRect area(scroll->viewport()->mapTo(host, QPoint()), scroll->viewport()->size());
    QHash<QString, QRect> before;
    QHash<QString, QImage> previousImages;
    qint64 bytes = 0;
    for (const auto& id : order) {
        if (sections.value(id)->bottom) continue;
        auto* frame = sections.value(id)->frame;
        before[id] = QRect(frame->mapTo(host, QPoint()) - area.topLeft(), frame->size());
        bytes += qint64(frame->width() * frame->devicePixelRatioF()) * qCeil(frame->height() * frame->devicePixelRatioF()) * 4;
    }
    if (bytes > 32 * 1024 * 1024) { apply(); emit sectionLayoutChanged(); return true; }
    for (const auto& id : order) if (!sections.value(id)->bottom) previousImages[id] = sections.value(id)->frame->grab().toImage();
    apply();
    qint64 combinedBytes = qint64(area.width() * devicePixelRatioF()) * qCeil(area.height() * devicePixelRatioF()) * 4;
    for (const auto& id : order) {
        if (sections.value(id)->bottom) continue;
        const auto size = before[id].size().expandedTo(sections.value(id)->frame->size());
        combinedBytes += qint64(size.width() * devicePixelRatioF()) * qCeil(size.height() * devicePixelRatioF()) * 4;
    }
    if (combinedBytes > 48 * 1024 * 1024) { emit sectionLayoutChanged(); return true; }
    QList<PanelMotionLayer> layers;
    QImage backdrop(qCeil(area.width() * devicePixelRatioF()), qCeil(area.height() * devicePixelRatioF()), QImage::Format_ARGB32_Premultiplied);
    backdrop.setDevicePixelRatio(devicePixelRatioF());
    backdrop.fill(Qt::transparent);
    // The stack covers the viewport, including the empty area below sections.
    // Its actual style background can differ from the viewport's palette roles.
    stack->render(&backdrop, QPoint(), QRegion(QRect(-stack->pos(), area.size())),
                  QWidget::DrawWindowBackground);
    layers.append(PanelMotionLayer::stationary(backdrop));
    for (const auto& id : order) {
        if (sections.value(id)->bottom) continue;
        auto* frame = sections.value(id)->frame;
        const QRect after(frame->mapTo(host, QPoint()) - area.topLeft(), frame->size());
        const auto current = frame->grab().toImage();
        const auto old = previousImages.value(id);
        QImage combined(old.size().expandedTo(current.size()), QImage::Format_ARGB32_Premultiplied);
        combined.setDevicePixelRatio(devicePixelRatioF());
        combined.fill(frame->palette().color(QPalette::Window));
        { QPainter painter(&combined); painter.drawImage(QPoint(), old); painter.drawImage(QPoint(), current); }
        layers.append({combined, collapsed ? after.topLeft() : before[id].topLeft(),
                        collapsed ? before[id].topLeft() : after.topLeft(),
                        collapsed ? after.size() : before[id].size(),
                        collapsed ? before[id].size() : after.size()});
    }
    compositor->present(section->frame, area, std::move(layers), collapsed, !collapsed);
    emit sectionLayoutChanged();
    return true;
}
int ContextDockHost::minimumSectionHeight(const Section* section) const
{ return section->header->height() + resizeHeight + qMax(48, section->view->minimumHeight()); }
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
bool ContextDockHost::moveResource(const QString& key, int index)
{
    const int old = order.indexOf(key);
    if (old < 0) return false;
    index = qBound(0, index, int(order.size()) - 1);
    if (old == index) return true;
    settleMotion();
    order.move(old, index);
    arrangeSections();
    emit resourceOrderChanged();
    return true;
}
void ContextDockHost::settleMotion()
{
    if (auto* host = qobject_cast<QMainWindow*>(window()))
        if (auto* compositor = host->findChild<PanelCompositor*>()) compositor->settle();
}

void ContextDockHost::arrangeSections()
{
    if (arranging || !bottomScroll || !bottomStack) return;
    arranging = true;
    arrangeArea(false);
    arrangeArea(true);
    arranging = false;
}

void ContextDockHost::arrangeArea(bool bottom)
{
    QScrollArea* areaScroll = bottom ? bottomScroll : scroll;
    QWidget* areaStack = bottom ? bottomStack : stack;
    const QSize available = areaScroll->viewport()->size();
    const int extent = bottom ? available.width() : available.height();
    QStringList keys;
    for (const QString& key : order) if (sections.value(key)->bottom == bottom) keys.append(key);
    QList<int> minimums;
    QList<int> weights;
    int minimumSum = 0, weightSum = 0;
    for (const auto& key : keys) {
        auto* section = sections.value(key);
        const int minimum = bottom ? qMax(180, section->header->minimumSizeHint().width())
            : (section->collapsed ? section->header->height() : minimumSectionHeight(section));
        const int requested = bottom ? section->width : (section->retainedHeight > 0 ? section->retainedHeight : section->height);
        const int weight = !bottom && section->collapsed ? 0
            : qMax(1, (requested > 0 ? requested : extent / qMax(1, int(keys.size()))) - minimum);
        minimums.append(minimum);
        weights.append(weight);
        minimumSum += minimum;
        weightSum += weight;
    }
    const int extra = qMax(0, extent - minimumSum);
    int position = 0, allocated = 0, accumulatedWeight = 0;
    for (int i = 0; i < keys.size(); ++i) {
        auto* section = sections.value(keys.at(i));
        accumulatedWeight += weights.at(i);
        const int nextAllocation = weightSum ? qint64(extra) * accumulatedWeight / weightSum : 0;
        const int length = minimums.at(i) + nextAllocation - allocated;
        allocated = nextAllocation;
        section->frame->setGeometry(bottom ? QRect(position, 0, length, available.height())
                                           : QRect(0, position, available.width(), length));
        section->view->setVisible(!section->collapsed);
        section->resize->setVisible(i + 1 < keys.size());
        section->resize->setGeometry(bottom ? QRect(length - resizeHeight, 0, resizeHeight, available.height())
                                            : QRect(0, length - resizeHeight, available.width(), resizeHeight));
        section->resize->raise();
        section->frame->layout()->setContentsMargins(0, 0,
            bottom && i + 1 < keys.size() ? resizeHeight : 0,
            !bottom && i + 1 < keys.size() ? resizeHeight : 0);
        section->frame->layout()->setGeometry(section->frame->rect());
        position += length;
    }
    areaStack->resize(bottom ? QSize(qMax(position, available.width()), available.height())
                            : QSize(available.width(), qMax(position, available.height())));
}
void ContextDockHost::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    for (auto* section : sections) section->retainedHeight = 0;
    arrangeSections();
    QTimer::singleShot(0, this, [this] {
        for (const QString& key : order) {
            auto* view = sections.value(key)->view;
            const auto layouts = view->findChildren<QLayout*>();
            for (auto* layout : layouts) { layout->invalidate(); layout->activate(); }
        }
        arrangeSections();
    });
}
int ContextDockHost::insertionIndex(const QPoint& globalPosition) const
{
    const bool bottom = isBottomPosition(globalPosition);
    const QPoint point = (bottom ? bottomStack : stack)->mapFromGlobal(globalPosition);
    const int axis = bottom ? point.x() : point.y();
    int end = order.size();
    for (int i = 0; i < order.size(); ++i) {
        if (sections.value(order[i])->bottom != bottom) continue;
        const QRect bounds = sections.value(order[i])->frame->geometry();
        if (axis < (bottom ? bounds.center().x() : bounds.center().y())) return i;
        end = i + 1;
    }
    return end;
}
bool ContextDockHost::isBottomPosition(const QPoint& position) const
{ return bottomRoot && bottomRoot->isVisible() && bottomRoot->rect().contains(bottomRoot->mapFromGlobal(position)); }
bool ContextDockHost::containsDropPosition(const QPoint& position) const
{ return (isVisible() && rect().contains(mapFromGlobal(position))) || isBottomPosition(position); }
void ContextDockHost::finishSectionDrag(const QString& key, const QPoint& position)
{
    if (!sections.contains(key)) return;
    if (containsDropPosition(position)) moveResourceToArea(key, isBottomPosition(position), insertionIndex(position));
    else if (sections.value(key)->detachable) emit dragOutRequested(key, position);
    emit sectionDragFinished();
}
void ContextDockHost::showInsertion(const QPoint& globalPosition)
{
    if (!containsDropPosition(globalPosition)) { insertionMarker->hide(); return; }
    const bool bottom = isBottomPosition(globalPosition);
    QWidget* areaStack = bottom ? bottomStack : stack;
    insertionMarker->setParent(areaStack);
    const int index = insertionIndex(globalPosition);
    int position = 0;
    for (int i = 0; i < order.size(); ++i) {
        const auto* section = sections.value(order.at(i));
        if (section->bottom != bottom) continue;
        if (i >= index) break;
        position = bottom ? section->frame->geometry().right() + 1 : section->frame->geometry().bottom() + 1;
    }
    insertionMarker->setGeometry(bottom ? QRect(position, 0, 3, areaStack->height())
                                       : QRect(0, position, areaStack->width(), 3));
    insertionMarker->show();
    insertionMarker->raise();
}
bool ContextDockHost::eventFilter(QObject* watched, QEvent* event)
{
    if (((scroll && watched == scroll->viewport()) || (bottomScroll && watched == bottomScroll->viewport()))
        && event->type() == QEvent::Resize) arrangeSections();
    if (watched == bottomRoot) {
        if (event->type() == QEvent::DragEnter) { dragEnterEvent(static_cast<QDragEnterEvent*>(event)); return true; }
        if (event->type() == QEvent::DragMove) {
            auto* drag = static_cast<QDragMoveEvent*>(event);
            if (validFloatingSource(drag->source(), drag->mimeData())) {
                showInsertion(bottomRoot->mapToGlobal(drag->position().toPoint()));
                drag->acceptProposedAction();
            }
            return true;
        }
        if (event->type() == QEvent::DragLeave) { dragLeaveEvent(static_cast<QDragLeaveEvent*>(event)); return true; }
        if (event->type() == QEvent::Drop) {
            auto* drop = static_cast<QDropEvent*>(event);
            insertionMarker->hide();
            if (validFloatingSource(drop->source(), drop->mimeData())
                && acceptFloatingDrop(qobject_cast<ContextFloatingWindow*>(drop->source()),
                    QString::fromUtf8(drop->mimeData()->data(resourceMimeType())), bottomRoot->mapToGlobal(drop->position().toPoint()))) {
                drop->setDropAction(Qt::MoveAction);
                drop->accept();
            }
            return true;
        }
    }
    const QString key = watched->property("contextResourceKey").toString();
    if (sections.contains(key)) {
        auto* section = sections.value(key);
        if (watched == section->view && event->type() == QEvent::DynamicPropertyChange) {
            const QByteArray name = static_cast<QDynamicPropertyChangeEvent*>(event)->propertyName();
            if (name == "contextStatusText" || name == "contextStatusTooltip"
                || name == "contextScopeText" || name == "contextScopeTooltip"
                || name == "contextFitAvailable") refreshSectionStatus(key);
            else if (name == "contextDisplayTitle") section->title->setText(displayTitle(resources.value(key), section->view));
            return false;
        }
        if (watched->property("elaDragManaged").toBool())
            return false;
        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) {
                focusSection(key);
                draggedKey = key;
                dragStart = mouse->globalPosition().toPoint();
                resizingSection = watched == section->resize;
                resizeStartHeight = section->frame->height();
                resizeStartWidth = section->frame->width();
                resizeNeighbor.clear();
                resizeLengths.clear();
                if (resizingSection) {
                    settleMotion();
                    bool after = false;
                    for (const auto& id : order) {
                        const auto* other = sections.value(id);
                        if (other->bottom != section->bottom) continue;
                        resizeLengths[id] = section->bottom ? other->frame->width() : other->frame->height();
                        if (after && resizeNeighbor.isEmpty()) resizeNeighbor = id;
                        if (id == key) after = true;
                    }
                }
                return true;
            }
        } else if (event->type() == QEvent::MouseMove && draggedKey == key) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (resizingSection) {
                auto* neighbor = sections.value(resizeNeighbor);
                if (!neighbor) return true;
                const auto minimum = [this](Section* item) {
                    return item->bottom ? qMax(180, item->header->minimumSizeHint().width())
                        : (item->collapsed ? item->header->height() : minimumSectionHeight(item));
                };
                const QPoint delta = mouse->globalPosition().toPoint() - dragStart;
                const int movement = qBound(minimum(section) - resizeLengths.value(key),
                    section->bottom ? delta.x() : delta.y(),
                    resizeLengths.value(resizeNeighbor) - minimum(neighbor));
                for (auto it = resizeLengths.cbegin(); it != resizeLengths.cend(); ++it) {
                    auto* item = sections.value(it.key());
                    const int length = it.value() + (it.key() == key ? movement : (it.key() == resizeNeighbor ? -movement : 0));
                    item->retainedHeight = 0;
                    if (item->bottom) item->width = length;
                    else item->height = length;
                }
                arrangeSections();
                emit sectionLayoutChanged();
            }
            else if ((mouse->globalPosition().toPoint() - dragStart).manhattanLength() >= QApplication::startDragDistance()) {
                emit sectionDragStarted();
                showInsertion(mouse->globalPosition().toPoint());
            }
            return true;
        } else if (event->type() == QEvent::MouseButtonRelease && draggedKey == key) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            const QPoint position = mouse->globalPosition().toPoint();
            if (!resizingSection && (position - dragStart).manhattanLength() >= QApplication::startDragDistance()) {
                finishSectionDrag(key, position);
            }
            draggedKey.clear();
            insertionMarker->hide();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

bool ContextDockHost::setSectionDetachable(const QString& key, bool detachable)
{
    auto* section = sections.value(key);
    if (!section) return false;
    section->detachable = detachable;
    section->drag->setEnabled(detachable);
    section->drag->setToolTip(detachable ? tr("Drag to the sidebar or bottom area to dock; drag outside to float")
        : tr("This view cannot leave the sidebar by dragging. Use its preview action for the editor overlay."));
    return true;
}
QWidget* ContextDockHost::sectionDragHandle(const QString& key) const
{ auto* section = sections.value(key); return section ? section->drag : nullptr; }
bool ContextDockHost::validFloatingSource(QObject* source, const QMimeData* mime) const
{
    auto* floating = qobject_cast<ContextFloatingWindow*>(source);
    return floating && floating->parentWidget() && floating->parentWidget()->isAncestorOf(this) && floating->hasResource()
        && mime && mime->hasFormat(resourceMimeType())
        && floating->resource().stableKey() == QString::fromUtf8(mime->data(resourceMimeType()));
}
bool ContextDockHost::acceptFloatingDrop(ContextFloatingWindow* source, const QString& key, const QPoint& globalPosition)
{
    QMimeData mime; mime.setData(resourceMimeType(), key.toUtf8());
    if (!validFloatingSource(source, &mime) || !containsDropPosition(globalPosition)) return false;
    emit floatingDropRequested(key, insertionIndex(globalPosition), isBottomPosition(globalPosition));
    return containsResource(key) && !source->hasResource();
}
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
