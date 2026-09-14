#include "roundedicons.h"
#include "contextdockhost.h"
#include "contextfloatingwindow.h"
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

namespace {
constexpr int headerHeight = 28;
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
    QToolButton* drag;
    bool detachable = false;
    bool collapsed = false;
    int height = 0;
    int retainedHeight = 0;
};

ContextDockHost::ContextDockHost(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("contextDockHost"));
    setAcceptDrops(true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(false);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    stack = new QWidget;
    scroll->setWidget(stack);
    scroll->viewport()->installEventFilter(this);
    layout->addWidget(scroll);
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
ContextDockHost::~ContextDockHost() { disconnect(qApp, nullptr, this, nullptr); qDeleteAll(sections); }
int ContextDockHost::resourceCount() const { return order.size(); }
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
    section->toggle = new QToolButton(section->header);
    section->toggle->setArrowType(Qt::DownArrow);
    section->toggle->setToolTip(tr("Collapse or expand section"));
    row->addWidget(section->toggle);
    section->drag = new QToolButton(section->header);
    section->drag->setObjectName(QStringLiteral("contextSectionDrag"));
    section->drag->setIcon(style()->standardIcon(QStyle::SP_TitleBarNormalButton));
    section->drag->setCursor(Qt::OpenHandCursor);
    section->drag->setProperty("contextResourceKey", key);
    section->drag->installEventFilter(this);
    row->addWidget(section->drag);
    section->title = new QLabel(displayTitle(resource, view), section->header);
    section->title->setObjectName(QStringLiteral("contextSectionTitle"));
    section->title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    section->title->setAttribute(Qt::WA_TransparentForMouseEvents);
    row->addWidget(section->title, 1);
    // Freshness belongs on the title bar so the body can stay content.
    // A view publishes it through the contextStatus* properties; sections
    // whose view publishes nothing keep the header as it was.
    section->status = new QLabel(section->header);
    section->status->setObjectName(QStringLiteral("contextSectionStatus"));
    section->status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    section->status->setAttribute(Qt::WA_TransparentForMouseEvents);
    section->status->hide();
    row->addWidget(section->status);
    // Same property channel as the status chip, one step further: a view that
    // names its current scope gets a clickable chip for choosing another. The
    // host stays generic — it just invokes the view's own slot.
    section->scope = new QToolButton(section->header);
    section->scope->setObjectName(QStringLiteral("contextSectionScope"));
    section->scope->setCursor(Qt::PointingHandCursor);
    section->scope->hide();
    connect(section->scope, &QToolButton::clicked, this, [this, key] {
        auto* target = sections.value(key);
        if (target && target->view)
            QMetaObject::invokeMethod(target->view, "requestScopePick");
    });
    row->addWidget(section->scope);
    section->fullView = new QToolButton(section->header);
    section->fullView->setObjectName(QStringLiteral("contextDockFullView"));
    section->fullView->setIcon(RoundedIcons::icon(RoundedIcons::Expand));
    section->fullView->setToolTip(tr("Open in main area"));
    section->fullView->setVisible(fullViewAvailable);
    row->addWidget(section->fullView);
    auto* unpin = new QToolButton(section->header);
    unpin->setObjectName(QStringLiteral("contextDockUnpin"));
    unpin->setIcon(RoundedIcons::icon(RoundedIcons::Pin));
    unpin->setToolTip(tr("Move to preview"));
    row->addWidget(unpin);
    auto* close = new QToolButton(section->header);
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
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    column->setSizeConstraint(QLayout::SetNoConstraint);
    section->header->setFixedHeight(headerHeight);
    section->resize->setFixedHeight(resizeHeight);
    column->addWidget(section->header);
    column->addWidget(view, 1);
    column->addWidget(section->resize);
    sections.insert(key, section);
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
    focusedKey = key;
    arrangeSections();
    emit currentResourceChanged(resources.value(key));
}
bool ContextDockHost::activateResource(const QString& key)
{
    if (!sections.contains(key)) return false;
    setSectionCollapsed(key, false);
    focusSection(key);
    arrangeSections();
    scroll->ensureWidgetVisible(sections.value(key)->header);
    sections.value(key)->header->setFocus(Qt::OtherFocusReason);
    return true;
}
QWidget* ContextDockHost::takeResource(const QString& key)
{
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
bool ContextDockHost::setSectionCollapsed(const QString& key, bool collapsed)
{
    auto* section = sections.value(key);
    if (!section) return false;
    if (section->collapsed == collapsed) return true;
    for (auto* other : sections) other->retainedHeight = 0;
    section->collapsed = collapsed;
    section->toggle->setArrowType(collapsed ? Qt::RightArrow : Qt::DownArrow);
    arrangeSections();
    emit sectionLayoutChanged();
    return true;
}
int ContextDockHost::minimumSectionHeight(const Section* section) const
{ return headerHeight + resizeHeight + qMax(48, section->view->minimumHeight()); }
bool ContextDockHost::setSectionHeight(const QString& key, int height)
{
    auto* section = sections.value(key);
    if (!section) return false;
    const int bounded = height <= 0 ? 0 : qBound(minimumSectionHeight(section), height, 8192);
    if (section->height == bounded) return true;
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
    order.move(old, index);
    arrangeSections();
    emit resourceOrderChanged();
    return true;
}
void ContextDockHost::arrangeSections()
{
    if (arranging) return;
    arranging = true;
    QHash<QString, int> heights;
    int total = 0;
    int expanded = 0;
    for (const QString& key : order) if (!sections.value(key)->collapsed) ++expanded;
    const int defaultHeight = expanded ? qMax(0, scroll->viewport()->height() - (order.size() - expanded) * headerHeight) / expanded : 0;
    for (const QString& key : order) {
        auto* section = sections.value(key);
        const int requested = section->retainedHeight > 0 ? section->retainedHeight : (section->height > 0 ? section->height : defaultHeight);
        const int desired = section->collapsed ? headerHeight : qMax(minimumSectionHeight(section), requested);
        heights.insert(key, desired);
        total += desired;
    }
    int deficit = qMax(0, total - scroll->viewport()->height());
    QStringList compression = order;
    compression.removeAll(focusedKey);
    if (sections.contains(focusedKey)) compression.append(focusedKey);
    for (const QString& key : compression) {
        auto* section = sections.value(key);
        const int minimum = section->collapsed ? headerHeight : minimumSectionHeight(section);
        const int reduction = qMin(deficit, heights.value(key) - minimum);
        heights[key] -= reduction;
        deficit -= reduction;
    }
    int y = 0;
    const int width = scroll->viewport()->width();
    for (const QString& key : order) {
        auto* section = sections.value(key);
        const int height = heights.value(key);
        section->frame->setGeometry(0, y, width, height);
        section->view->setVisible(!section->collapsed);
        section->resize->setVisible(!section->collapsed);
        section->frame->layout()->setGeometry(section->frame->rect());
        y += height;
    }
    stack->resize(width, qMax(y, scroll->viewport()->height()));
    arranging = false;
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
    const int y = stack->mapFromGlobal(globalPosition).y();
    for (int i = 0; i < order.size(); ++i) {
        const QRect bounds = sections.value(order[i])->frame->geometry();
        if (y < bounds.center().y()) return i;
        if (y <= bounds.bottom()) return i + 1;
    }
    return order.size();
}
void ContextDockHost::showInsertion(const QPoint& globalPosition)
{
    const int index = insertionIndex(globalPosition);
    const int y = index < order.size() ? sections.value(order[index])->frame->y()
        : (order.isEmpty() ? 0 : sections.value(order.last())->frame->geometry().bottom());
    insertionMarker->setGeometry(0, y, stack->width(), 3);
    insertionMarker->show();
    insertionMarker->raise();
}
bool ContextDockHost::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == scroll->viewport() && event->type() == QEvent::Resize) arrangeSections();
    const QString key = watched->property("contextResourceKey").toString();
    if (sections.contains(key)) {
        auto* section = sections.value(key);
        if (watched == section->view && event->type() == QEvent::DynamicPropertyChange) {
            const QByteArray name = static_cast<QDynamicPropertyChangeEvent*>(event)->propertyName();
            if (name == "contextStatusText" || name == "contextStatusTooltip"
                || name == "contextScopeText" || name == "contextScopeTooltip") refreshSectionStatus(key);
            else if (name == "contextDisplayTitle") section->title->setText(displayTitle(resources.value(key), section->view));
            return false;
        }
        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) {
                focusSection(key);
                draggedKey = key;
                dragStart = mouse->globalPosition().toPoint();
                resizingSection = watched == section->resize;
                resizeStartHeight = section->frame->height();
                return true;
            }
        } else if (event->type() == QEvent::MouseMove && draggedKey == key) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (resizingSection) setSectionHeight(key, resizeStartHeight + mouse->globalPosition().toPoint().y() - dragStart.y());
            else if ((mouse->globalPosition().toPoint() - dragStart).manhattanLength() >= QApplication::startDragDistance())
                showInsertion(mouse->globalPosition().toPoint());
            return true;
        } else if (event->type() == QEvent::MouseButtonRelease && draggedKey == key) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            const QPoint position = mouse->globalPosition().toPoint();
            if (!resizingSection && (position - dragStart).manhattanLength() >= QApplication::startDragDistance()) {
                if (rect().contains(mapFromGlobal(position))) {
                    int index = insertionIndex(position);
                    if (index > order.indexOf(key)) --index;
                    moveResource(key, index);
                } else if (section->detachable) {
                    emit dragOutRequested(key, position);
                }
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
    section->drag->setToolTip(detachable ? tr("Drag outside the sidebar to float; drag within to reorder")
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
    if (!validFloatingSource(source, &mime) || !rect().contains(mapFromGlobal(globalPosition))) return false;
    emit floatingDropRequested(key, insertionIndex(globalPosition));
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
