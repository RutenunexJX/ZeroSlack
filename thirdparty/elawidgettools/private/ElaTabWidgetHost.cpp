#include "ElaTabWidgetHost.h"

#include "ElaAppBar.h"
#include "ElaTabBar.h"
#include <QApplication>
#include <QSignalBlocker>
#include <QCloseEvent>
#include <QCursor>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QScopeGuard>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
// Pointer-bearing drag data is in-process and guarded. Serialized/foreign MIME
// cannot reparent a page or cross document-controller scopes.
class HostedTabMime final : public QMimeData {
public:
    ~HostedTabMime() override {
        for (const auto& window : floatingWindows)
            if (window && window->windowHandle())
                window->windowHandle()->setFlag(Qt::WindowTransparentForInput, false);
    }
    QPointer<ElaTabWidget> source;
    QPointer<QWidget> page;
    QPointer<QObject> scope;
    QList<QPointer<QWidget>> floatingWindows;
    QList<QPair<QPointer<ElaTabWidget>, QPointer<QWidget>>> previousSelections;
    bool committed{false};
    bool executing{false};
    bool cancelled{false};
};

QPointer<QObject> activeDragScope;
QPointer<QWidget> activeDragIndicator;

void followFloatingPage(HostedTabMime* data)
{
    if (!data->scope || !data->page || !data->source
        || !data->source->isTabVisible(data->source->indexOf(data->page))) {
        data->cancelled = true;
        if (data->executing) QDrag::cancel();
        return;
    }
    if (!data->source || !data->source->isFloatingTabWidget())
        return;
    QWidget* window = data->source->window();
    if (!data->floatingWindows.contains(window))
        data->floatingWindows.append(window);
    window->move(QCursor::pos() - QPoint(24, 16));
    if (window->windowHandle())
        window->windowHandle()->setFlag(Qt::WindowTransparentForInput, true);
}

class DropIndicator final : public QWidget {
public:
    explicit DropIndicator(QWidget* parent) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setObjectName(QStringLiteral("elaHostedTabDropIndicator"));
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        QColor accent = palette().color(QPalette::Highlight);
        painter.setPen(QPen(accent, 1));
        accent.setAlpha(40);
        painter.setBrush(accent);
        painter.drawRoundedRect(rect().adjusted(1, 1, -2, -2), 6, 6);
    }
};

bool releasedOutside(bool cancelled)
{
    if (cancelled)
        return false;
#ifdef Q_OS_WIN
    // OLE handles Escape without delivering a Qt KeyPress. A cancelled drag
    // returns while the initiating button is still held (or Escape is down).
    if (QGuiApplication::platformName() == QStringLiteral("windows"))
        return !(GetAsyncKeyState(VK_LBUTTON) & 0x8000)
            && !(GetAsyncKeyState(VK_ESCAPE) & 0x8000);
#endif
    return !(QApplication::mouseButtons() & Qt::LeftButton);
}
}

class ElaHostedTabWindow final : public QWidget {
public:
    ElaHostedTabWindow(QWidget* owner, ElaTabWidgetHost* origin)
        : QWidget(owner, Qt::Window), tabs(new ElaTabWidget(this)) {
        setObjectName(QStringLiteral("elaHostedTabWindow"));
        setAttribute(Qt::WA_DeleteOnClose);
        bar = new ElaAppBar(this);
        bar->setWindowButtonFlags(ElaAppBarType::MinimizeButtonHint
            | ElaAppBarType::MaximizeButtonHint | ElaAppBarType::CloseButtonHint);
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(tabs);
        tabs->setHostedTabs(origin->scope, origin->split, origin->returnTarget);
        tabs->_host->floatingWindow = this;
        connect(tabs, &QTabWidget::currentChanged, this, [this](int index) {
            setWindowTitle(index >= 0 ? tabs->tabText(index) : QString());
        });
        resize(800, 600);
    }
    ElaTabWidget* tabs;
protected:
    void closeEvent(QCloseEvent* event) override {
        auto* host = tabs->_host;
        // Closing the Ela container returns views. Only the host application's
        // tab-close action can destroy a document after its unsaved check.
        while (tabs->count() > 0) {
            ElaTabWidget* target = host->scope && host->returnTarget ? host->returnTarget() : nullptr;
            if (!target || target == tabs || !host->transfer(tabs->widget(0), target, -1)) {
                event->ignore();
                return;
            }
        }
        event->accept();
    }
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray& type, void* message, qintptr* result) override {
        if (bar) {
            const int handled = bar->takeOverNativeEvent(type, message, result);
            if (handled >= 0)
                return handled != 0;
        }
        return QWidget::nativeEvent(type, message, result);
    }
#endif
private:
    ElaAppBar* bar{nullptr};
};

ElaTabWidgetHost::ElaTabWidgetHost(ElaTabWidget* widget, QObject* owner,
    ElaTabWidget::SplitResolver resolver, ElaTabWidget::ReturnTarget returnResolver)
    : QObject(widget), group(widget), scope(owner), split(std::move(resolver)),
      returnTarget(std::move(returnResolver))
{
    group->tabBar()->setAcceptDrops(true);
    group->tabBar()->setProperty("elaHostedTabOwner", QVariant::fromValue(static_cast<QObject*>(group)));
    qApp->installEventFilter(this);
}

ElaTabWidgetHost::~ElaTabWidgetHost() { clearIndicator(); }

bool ElaTabWidgetHost::isDragging() const
{
    return dragging || (scope && activeDragScope == scope);
}

bool ElaTabWidgetHost::acceptsDrag(const QMimeData* mime) const
{
    const auto* data = dynamic_cast<const HostedTabMime*>(mime);
    return data && scope && data->scope == scope && data->source && data->page
        && !data->cancelled && data->source->indexOf(data->page) >= 0
        && data->source->isTabVisible(data->source->indexOf(data->page));
}

bool ElaTabWidgetHost::transfer(QWidget* page, ElaTabWidget* target, int index)
{
    if (!scope || !group || !page || !target || !target->_host
        || target->_host->scope != scope)
        return false;
    const int from = group->indexOf(page);
    if (from < 0)
        return false;
    if (target == group) {
        if (index >= 0)
            group->tabBar()->moveTab(from, qBound(0, index, group->count() - 1));
        group->setCurrentWidget(page);
        return true;
    }
    const QString text = group->tabText(from);
    const QIcon icon = group->tabIcon(from);
    const QString tip = group->tabToolTip(from);
    const QString whatsThis = group->tabWhatsThis(from);
    const QVariant data = group->tabBar()->tabData(from);
    const QColor color = group->tabBar()->tabTextColor(from);
    const bool visible = group->isTabVisible(from);
    const bool enabled = group->isTabEnabled(from);
    QPointer<ElaTabWidget> sourceGuard(group), targetGuard(target);
    QPointer<QWidget> pageGuard(page), focused(QApplication::focusWidget());
    QPointer<QWidget> sourceCurrent(group->currentWidget()), targetCurrent(target->currentWidget());
    const auto insert = [&](ElaTabWidget* destination, int position) {
        QPointer<ElaTabWidget> destinationGuard(destination);
        const int inserted = destination->insertTab(position < 0 ? destination->count() : position, pageGuard, icon, text);
        if (!destinationGuard || !pageGuard || inserted < 0) return false;
        destination->setTabToolTip(inserted, tip);
        destination->setTabWhatsThis(inserted, whatsThis);
        destination->tabBar()->setTabData(inserted, data);
        destination->tabBar()->setTabTextColor(inserted, color);
        destination->setTabEnabled(inserted, enabled);
        destination->setTabVisible(inserted, visible);
        return true;
    };
    {
        // A move publishes only its committed state. Intermediate currentChanged
        // notifications otherwise close interaction modes and can destroy the target.
        QSignalBlocker sourceBlock(group), targetBlock(target);
        group->removeTab(from);
        bool transferred = sourceGuard && targetGuard && pageGuard;
        if (transferred) transferred = insert(targetGuard,index);
        if (!sourceGuard) sourceBlock.dismiss();
        if (!targetGuard) targetBlock.dismiss();
        if (!transferred || !sourceGuard || !targetGuard || !pageGuard) {
            if (sourceGuard && pageGuard) {
                if (targetGuard && targetGuard->indexOf(pageGuard)>=0)
                    targetGuard->removeTab(targetGuard->indexOf(pageGuard));
                insert(sourceGuard,from);
                if (sourceCurrent) sourceGuard->setCurrentWidget(sourceCurrent);
            }
            if (targetGuard && targetCurrent) targetGuard->setCurrentWidget(targetCurrent);
            if (focused) focused->setFocus(Qt::OtherFocusReason);
            return false;
        }
        if (visible) target->setCurrentWidget(page);
    }
    if (focused && (focused==page || page->isAncestorOf(focused)))
        focused->setFocus(Qt::OtherFocusReason);
    emit sourceGuard->hostedTabMoved(page, sourceGuard, targetGuard);
    if (targetGuard)
        targetGuard->syncFloatingVisibility();
    if (sourceGuard)
        sourceGuard->syncFloatingVisibility();
    return true;
}

ElaTabWidget* ElaTabWidgetHost::detach(QWidget* page, const QPoint& position)
{
    if (!scope || !group || group->indexOf(page) < 0
        || (page->property("hostedTabDetachable").isValid() && !page->property("hostedTabDetachable").toBool()))
        return nullptr;
    if (floatingWindow && group->count() == 1) {
        floatingWindow->move(position);
        return group;
    }
    QWidget* owner = floatingWindow ? floatingWindow->parentWidget() : group->window();
    auto* window = new ElaHostedTabWindow(owner, this);
    QPointer<ElaTabWidget> tabs(window->tabs);
    emit group->floatingTabWidgetCreated(tabs);
    if (!tabs || !transfer(page, tabs, -1)) {
        delete window;
        return nullptr;
    }
    if (QScreen* screen = QGuiApplication::screenAt(position)) {
        const QRect available = screen->availableGeometry();
        window->resize(window->size().boundedTo(available.size()));
        window->move(qBound(available.left(), position.x(), available.right() - window->width() + 1),
                     qBound(available.top(), position.y(), available.bottom() - window->height() + 1));
    } else {
        window->move(position);
    }
    window->show();
    return tabs;
}

void ElaTabWidgetHost::syncVisibility()
{
    if (!floatingWindow || !group)
        return;
    bool visible = false;
    for (int i = 0; i < group->count(); ++i)
        visible |= group->isTabVisible(i);
    floatingWindow->setVisible(visible);
}

bool ElaTabWidgetHost::belongsToGroup(QObject* object) const
{
    auto* widget = qobject_cast<QWidget*>(object);
    while (widget) {
        if (widget == group->tabBar()) return true;
        if (auto* tabs = qobject_cast<ElaTabWidget*>(widget))
            return tabs == group;
        widget = widget->parentWidget();
    }
    return false;
}

ElaTabWidget::DropArea ElaTabWidgetHost::areaAt(const QPoint& position) const
{
    using Area = ElaTabWidget::DropArea;
    if (group->count() == 0 || floatingWindow
        || group->tabBar()->rect().contains(group->tabBar()->mapFromGlobal(group->mapToGlobal(position))))
        return Area::Center;
    const qreal x = qreal(position.x()) / qMax(1, group->width());
    const qreal y = qreal(position.y()) / qMax(1, group->height());
    const qreal edge = qMin(qMin(x, 1 - x), qMin(y, 1 - y));
    if (edge >= 0.25)
        return Area::Center;
    if (edge == x) return Area::Left;
    if (edge == 1 - x) return Area::Right;
    return edge == y ? Area::Top : Area::Bottom;
}

void ElaTabWidgetHost::showIndicator(ElaTabWidget::DropArea area)
{
    if (!indicator)
        indicator = new DropIndicator(group);
    if (activeDragIndicator && activeDragIndicator!=indicator) activeDragIndicator->hide();
    activeDragIndicator=indicator;
    QRect areaRect = group->rect().adjusted(3, 3, -3, -3);
    using Area = ElaTabWidget::DropArea;
    if (area == Area::Left) areaRect.setWidth(areaRect.width() / 2);
    if (area == Area::Right) areaRect.setLeft(areaRect.center().x());
    if (area == Area::Top) areaRect.setHeight(areaRect.height() / 2);
    if (area == Area::Bottom) areaRect.setTop(areaRect.center().y());
    indicator->setGeometry(areaRect);
    indicator->show();
    indicator->raise();
}

void ElaTabWidgetHost::clearIndicator()
{
    delete indicator.data();
    indicator = nullptr;
}

void ElaTabWidgetHost::startDrag(QMimeData* gesture)
{
    if (!gesture)
        return;
    gesture->deleteLater();
    if (!scope || !group || isDragging() || !group->currentWidget())
        return;
    const auto clearPreview=qScopeGuard([] {
        if (activeDragIndicator) activeDragIndicator->hide();
        activeDragIndicator.clear();
    });
    // ElaTabBar and ElaTabWidgetPrivate own gesture/enter/leave routing. This
    // transaction adapts their live-window flow to externally owned documents.
    QPointer<ElaTabWidgetHost> guard(this);
    QPointer<ElaTabWidget> origin(group);
    QPointer<QWidget> page(group->currentWidget());
    const int originIndex = group->indexOf(page);
    auto* data = new HostedTabMime;
    data->source = group;
    data->page = page;
    data->scope = scope;
    data->setData(QStringLiteral("application/x-ela-hosted-tab"), QByteArrayLiteral("1"));
    data->setProperty("DragType", "ElaTabBarDrag");
    data->setProperty("ElaHostedTabDrag", true);
    data->setProperty("TabSize", gesture->property("TabSize"));
    QPointer<QDrag> drag(new QDrag(group));
    drag->setProperty("ElaHostedTabPage", QVariant::fromValue(page.data()));
    drag->setMimeData(data);
    connect(page, &QObject::destroyed, drag, [data] { if (data->executing) QDrag::cancel(); });
    connect(scope, &QObject::destroyed, drag, [data] { if (data->executing) QDrag::cancel(); });
    QPixmap transparent(1, 1);
    transparent.fill(Qt::transparent);
    drag->setPixmap(transparent);
    dragging = true;
    cancelled = false;
    activeDragScope = scope;
    // End QTabBar's internal reorder gesture before entering the native loop.
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(-1, -1),
                        QPointF(group->tabBar()->mapToGlobal(QPoint(-1, -1))),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(group->tabBar(), &release);
    data->source = detach(page, QCursor::pos() - QPoint(24, 16));
    if (!data->source) {
        dragging = false;
        activeDragScope.clear();
        drag->deleteLater();
        return;
    }
    followFloatingPage(data);
    QTimer follow;
    follow.setInterval(10); // Same live content tracking cadence as upstream.
    const QPointer<QMimeData> dataGuard(data);
    connect(&follow, &QTimer::timeout, &follow, [dataGuard, data] {
        if (dataGuard) followFloatingPage(data);
    });
    follow.start();
    emit group->hostedTabDragStarted(drag);
    if (!guard || !drag || !dataGuard) {
        activeDragScope.clear();
        if (drag) drag->deleteLater();
        return;
    }
    followFloatingPage(data);
    data->executing = true;
    Qt::DropAction result = Qt::IgnoreAction;
    if (!cancelled && !data->cancelled && !data->committed && drag && page && scope) {
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() == QStringLiteral("windows") && origin) {
            // Qt 6.10 waits for a mouse move on its input HWND before OLE starts.
            // Reparenting a native view can focus the transparent floating host,
            // which cannot receive that move. Keep input on the source Qt window.
            origin->tabBar()->setFocus(Qt::OtherFocusReason);
            const QPointer<QWindow> input = origin->window()->windowHandle();
            if (input) {
                const HWND hwnd = reinterpret_cast<HWND>(input->winId());
                if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                    // The user may release while WebView2 is being reparented.
                    // Commit through the same drop transaction without starting
                    // a new native loop after that gesture has already ended.
                    if (!(GetAsyncKeyState(VK_ESCAPE) & 0x8000)) {
                        // Qt's widgetAt still sees the transparent Ela title bar.
                        // The gesture has ended: remove its live preview from hit
                        // testing, then resolve the actual window below it.
                        for (const auto& preview : data->floatingWindows)
                            if (preview) preview->hide();
                        for (QWidget* target = QApplication::widgetAt(QCursor::pos()); target; target = target->parentWidget()) {
                            auto* tabs = qobject_cast<ElaTabWidget*>(target);
                            if (!tabs) tabs = qobject_cast<ElaTabWidget*>(target->property("elaHostedTabOwner").value<QObject*>());
                            if (tabs && tabs != data->source && tabs->_host && tabs->_host->acceptsDrag(data)) {
                                if (tabs->_host->commitDrop(data, tabs->mapFromGlobal(QCursor::pos()))) result = Qt::MoveAction;
                                break;
                            }
                        }
                    } else cancelled = true;
                } else {
                    SetFocus(hwnd);
                    if (input->isVisible()) input->setMouseGrabEnabled(true);
                    const auto releaseCapture = qScopeGuard([input] { if (input && input->isVisible()) input->setMouseGrabEnabled(false); });
                    POINT cursor{};
                    GetCursorPos(&cursor);
                    ScreenToClient(hwnd, &cursor);
                    PostMessageW(hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(cursor.x, cursor.y));
                    result = drag->exec(Qt::MoveAction);
                }
            } else cancelled = true;
        } else
#endif
            result = drag->exec(Qt::MoveAction);
    }
    follow.stop();
    if (dataGuard) data->executing = false;
    if (!guard || !drag || !dataGuard) {
        activeDragScope.clear();
        return;
    }
    for (const auto& window : data->floatingWindows)
        if (window && window->windowHandle())
            window->windowHandle()->setFlag(Qt::WindowTransparentForInput, false);
    if (data->source) data->source->syncFloatingVisibility();
    if (!guard)
        return;
    clearIndicator();
    if (!data->committed && result == Qt::IgnoreAction && !releasedOutside(cancelled || data->cancelled)
        && data->source && page) {
        ElaTabWidget* target = origin ? origin.data() : (scope && returnTarget ? returnTarget() : nullptr);
        if (target)
            data->source->transferHostedTab(page, target, originIndex);
    }
    for (const auto& selection : data->previousSelections) {
        auto* visited = selection.first.data(); auto* selected = selection.second.data();
        if (visited && selected && visited->indexOf(selected) >= 0 && visited->indexOf(page) < 0) {
            const QSignalBlocker block(visited);
            visited->setCurrentWidget(selected);
        }
    }
    dragging = false;
    activeDragScope.clear();
    if (guard && group)
        emit group->hostedTabDragFinished();
    if (drag)
        drag->deleteLater();
}

bool ElaTabWidgetHost::enterDrag(QMimeData* mime)
{
    if (!acceptsDrag(mime))
        return false;
    auto* data = static_cast<HostedTabMime*>(mime);
    if (data->source != group && group->currentWidget()) {
        bool recorded = false;
        for (const auto& selection : data->previousSelections) if (selection.first == group) recorded = true;
        if (!recorded) data->previousSelections.append({group, group->currentWidget()});
    }
    if (!data->source->transferHostedTab(data->page, group, mime->property("TabDropIndex").toInt()))
        return false;
    data->source = group;
    return true;
}

void ElaTabWidgetHost::leaveDrag(QMimeData* mime)
{
    if (!acceptsDrag(mime))
        return;
    auto* data = static_cast<HostedTabMime*>(mime);
    if (data->source != group)
        return;
    if (auto* floating = detach(data->page, QCursor::pos() - QPoint(24, 16))) {
        data->source = floating;
        followFloatingPage(data);
    }
}

bool ElaTabWidgetHost::dropDrag(QMimeData* mime)
{
    if (!enterDrag(mime))
        return false;
    auto* data=static_cast<HostedTabMime*>(mime);
    data->committed = true;
    group->setCurrentWidget(data->page);
    if (data->page) data->page->setFocus(Qt::OtherFocusReason);
    return true;
}

bool ElaTabWidgetHost::commitDrop(QMimeData* mime, const QPoint& position)
{
    if (!acceptsDrag(mime)) return false;
    auto* data = static_cast<HostedTabMime*>(mime);
    const auto area = areaAt(position);
    ElaTabWidget* target = group;
    if (area != ElaTabWidget::DropArea::Center && split) target = split(group, area);
    const int index = area == ElaTabWidget::DropArea::Center
        ? group->tabBar()->tabAt(group->tabBar()->mapFromGlobal(group->mapToGlobal(position))) : -1;
    if (!target || !data->source->transferHostedTab(data->page, target, index)) return false;
    data->source = target; data->committed = true;
    if (data->page) data->page->setFocus(Qt::OtherFocusReason);
    return true;
}

bool ElaTabWidgetHost::eventFilter(QObject* watched, QEvent* event)
{
    if (!scope || !group)
        return false;
    if (event->type() == QEvent::KeyPress
        && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
        if (dragging) cancelled = true;
        clearIndicator();
    }
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseMove:
    case QEvent::MouseButtonRelease:
    case QEvent::DragEnter:
    case QEvent::DragMove:
    case QEvent::DragLeave:
    case QEvent::Drop:
        break;
    default:
        return false;
    }
    if (!belongsToGroup(watched))
        return false;
    if (event->type() == QEvent::DragLeave) {
        clearIndicator();
        return false;
    }
    if (event->type() != QEvent::DragEnter && event->type() != QEvent::DragMove && event->type() != QEvent::Drop)
        return false;
    auto* drop = static_cast<QDropEvent*>(event);
    auto* data = const_cast<HostedTabMime*>(dynamic_cast<const HostedTabMime*>(drop->mimeData()));
    if (!data && drop->mimeData()->property("DragType").toString() != QStringLiteral("ElaTabBarDrag"))
        return false;
    if (!acceptsDrag(drop->mimeData())) {
        drop->ignore();
        return true;
    }
    if (watched == group->tabBar())
        return false; // The original ElaTabBar enter/leave/drop route handles this.
    auto* widget = qobject_cast<QWidget*>(watched);
    const QPoint position = group->mapFromGlobal(widget->mapToGlobal(drop->position().toPoint()));
    const auto area = areaAt(position);
    if (event->type() != QEvent::Drop) {
        showIndicator(area);
        drop->setDropAction(Qt::MoveAction);
        drop->accept();
        return true;
    }
    clearIndicator();
    if (commitDrop(data, position)) {
        drop->setDropAction(Qt::MoveAction);
        drop->accept();
    } else {
        drop->ignore();
    }
    return true;
}
