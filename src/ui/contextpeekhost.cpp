#include "uicontrols.h"
#include "roundedicons.h"
#include "contextpeekhost.h"

#include "contextworkspacestate.h"

#include <QEnterEvent>
#include <QEvent>
#include <QDynamicPropertyChangeEvent>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QResizeEvent>
#include <QShortcut>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr int kMinimumRemainingEditorWidth = 240;
constexpr int kMinimumResizeHitExtent = 8;
constexpr int kMinimumCornerHitExtent = 16;

enum class HandleKind {
    Left,
    Bottom,
    Corner
};

class ContextPeekResizeHandle final : public QWidget
{
public:
    ContextPeekResizeHandle(HandleKind kindValue,
                            QWidget* parent)
        : QWidget(parent)
        , kind(kindValue)
    {
        setAttribute(Qt::WA_StyledBackground, false);
        setFocusPolicy(Qt::NoFocus);
        setMouseTracking(true);
    }

    void setPressed(bool value)
    {
        if (pressed == value)
            return;
        pressed = value;
        update();
    }

protected:
    void enterEvent(QEnterEvent* event) override
    {
        hovered = true;
        update();
        QWidget::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override
    {
        hovered = false;
        update();
        QWidget::leaveEvent(event);
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        const QPalette activePalette = palette();
        QColor surface = activePalette.color(QPalette::Highlight);
        surface.setAlpha(pressed ? 92 : (hovered ? 58 : 28));
        painter.fillRect(rect(), surface);

        QColor line = hovered || pressed
            ? activePalette.color(QPalette::Highlight)
            : activePalette.color(QPalette::Mid);
        QPen pen(line, pressed ? 2.0 : 1.0);
        pen.setCosmetic(true);
        painter.setPen(pen);

        if (kind == HandleKind::Left) {
            const int x = width() / 2;
            painter.drawLine(x, 0, x, height());
            const int center = height() / 2;
            for (int offset : {-6, 0, 6}) {
                painter.drawLine(qMax(0, x - 2), center + offset,
                                 qMin(width() - 1, x + 2), center + offset);
            }
        } else if (kind == HandleKind::Bottom) {
            const int y = height() / 2;
            painter.drawLine(0, y, width(), y);
            const int center = width() / 2;
            for (int offset : {-6, 0, 6}) {
                painter.drawLine(center + offset, qMax(0, y - 2),
                                 center + offset, qMin(height() - 1, y + 2));
            }
        } else {
            const int inset = qMax(3, qMin(width(), height()) / 4);
            painter.drawLine(inset, height() - inset,
                             width() - inset, inset);
            painter.drawLine(inset + 4, height() - inset,
                             width() - inset, inset + 4);
        }
    }

private:
    HandleKind kind;
    bool hovered = false;
    bool pressed = false;
};

int resizeHandleThickness(const QWidget* widget)
{
    if (!widget || !widget->style())
        return kMinimumResizeHitExtent;
    return qMax(
        kMinimumResizeHitExtent,
        widget->style()->pixelMetric(
            QStyle::PM_SplitterWidth,
            nullptr,
            widget));
}
}

ContextPeekHost::ContextPeekHost(QWidget* editorRegion)
    : QWidget(editorRegion)
{
    setObjectName(QStringLiteral("contextPeekHost"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFocusPolicy(Qt::StrongFocus);
    buildUi();
    if (editorRegion)
        editorRegion->installEventFilter(this);
    hide();
}

ContextPeekHost::~ContextPeekHost()
{
    finishResize(false);
    if (parentWidget())
        parentWidget()->removeEventFilter(this);
}

bool ContextPeekHost::hasResource() const
{
    return currentResource.isValid() && currentView;
}

ContextResource ContextPeekHost::resource() const
{
    return currentResource;
}

QWidget* ContextPeekHost::view() const
{
    return currentView;
}

QSize ContextPeekHost::preferredSize() const
{
    return preferredSizeValue;
}

int ContextPeekHost::preferredWidth() const
{
    return preferredSizeValue.width();
}

int ContextPeekHost::preferredHeight() const
{
    return preferredSizeValue.height();
}

void ContextPeekHost::setPreferredSize(const QSize& size)
{
    const QSize bounded = boundedStoredSize(size);
    if (preferredSizeValue == bounded)
        return;
    preferredSizeValue = bounded;
    synchronizeGeometry();
}

void ContextPeekHost::setPreferredWidth(int width)
{
    setPreferredSize(QSize(width, preferredSizeValue.height()));
}

void ContextPeekHost::setPreferredHeight(int height)
{
    setPreferredSize(QSize(preferredSizeValue.width(), height));
}

void ContextPeekHost::setActionsAvailable(
    bool pinAvailable,
    bool fullViewAvailable)
{
    pinButton->setVisible(pinAvailable);
    pinButton->setEnabled(pinAvailable);
    fullViewButton->setEnabled(fullViewAvailable);
    refreshFullViewAction();
}

void ContextPeekHost::refreshFullViewAction()
{
    const QVariant visible = currentView ? currentView->property("contextFullViewActionVisible") : QVariant();
    fullViewButton->setVisible(fullViewButton->isEnabled() && (!visible.isValid() || visible.toBool()));
}

void ContextPeekHost::setView(
    const ContextResource& resourceValue,
    QWidget* viewValue)
{
    if (!resourceValue.isValid() || !viewValue)
        return;
    clearView();
    currentResource = resourceValue;
    currentView = viewValue;
    viewValue->installEventFilter(this);
    refreshFullViewAction();
    viewValue->setParent(contentHost);
    contentLayout->addWidget(viewValue);
    titleLabel->setText(
        resourceValue.title.isEmpty()
            ? resourceValue.uri.fileName()
            : resourceValue.title);
    titleLabel->setToolTip(resourceValue.uri.toString());
    synchronizeGeometry();
    show();
    raise();
    viewValue->show();
}

bool ContextPeekHost::updateResource(
    const ContextResource& resourceValue)
{
    if (!hasResource()
        || resourceValue.stableKey()
               != currentResource.stableKey()) {
        return false;
    }
    currentResource = resourceValue;
    titleLabel->setText(
        resourceValue.title.isEmpty()
            ? resourceValue.uri.fileName()
            : resourceValue.title);
    titleLabel->setToolTip(resourceValue.uri.toString());
    return true;
}

QWidget* ContextPeekHost::takeView()
{
    QWidget* result = currentView;
    if (result) {
        result->removeEventFilter(this);
        contentLayout->removeWidget(result);
        result->hide();
        result->setParent(nullptr);
    }
    currentView.clear();
    currentResource = {};
    titleLabel->clear();
    hide();
    return result;
}

void ContextPeekHost::clearView()
{
    QWidget* oldView = takeView();
    if (oldView)
        oldView->deleteLater();
}

bool ContextPeekHost::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == currentView && event && event->type() == QEvent::DynamicPropertyChange
        && static_cast<QDynamicPropertyChangeEvent*>(event)->propertyName() == "contextFullViewActionVisible") {
        refreshFullViewAction();
    }
    if ((watched == leftResizeHandle
         || watched == bottomResizeHandle
         || watched == cornerResizeHandle)
        && event) {
        return handleResizeEvent(
            static_cast<QWidget*>(watched), event);
    }
    if (watched == parentWidget() && event) {
        switch (event->type()) {
        case QEvent::Resize:
            finishResize(false);
            [[fallthrough]];
        case QEvent::Show:
        case QEvent::LayoutRequest:
            synchronizeGeometry();
            break;
        case QEvent::Hide:
            finishResize(false);
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ContextPeekHost::hideEvent(QHideEvent* event)
{
    finishResize(false);
    QWidget::hideEvent(event);
}

void ContextPeekHost::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateResizeHandleGeometry();
}

void ContextPeekHost::buildUi()
{
    rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    header = new QWidget(this);
    header->setObjectName(QStringLiteral("contextPeekHeader"));
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(8, 4, 4, 4);
    headerLayout->setSpacing(4);
    titleLabel = UiControls::label(header);
    titleLabel->setObjectName(QStringLiteral("contextPeekTitle"));
    titleLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    headerLayout->addWidget(titleLabel, 1);

    pinButton = UiControls::toolButton(header);
    pinButton->setObjectName(QStringLiteral("contextPeekPin"));
    pinButton->setIcon(
        RoundedIcons::icon(RoundedIcons::Pin));
    pinButton->setToolTip(tr("Pin to context workspace"));
    headerLayout->addWidget(pinButton);

    fullViewButton = UiControls::toolButton(header);
    fullViewButton->setObjectName(QStringLiteral("contextPeekFullView"));
    fullViewButton->setIcon(
        RoundedIcons::icon(RoundedIcons::Expand));
    fullViewButton->setToolTip(tr("Open in main area"));
    headerLayout->addWidget(fullViewButton);

    closeButton = UiControls::toolButton(header);
    closeButton->setObjectName(QStringLiteral("contextPeekClose"));
    closeButton->setIcon(
        style()->standardIcon(QStyle::SP_DialogCloseButton));
    closeButton->setToolTip(tr("Close preview"));
    headerLayout->addWidget(closeButton);
    rootLayout->addWidget(header);

    contentHost = new QWidget(this);
    contentHost->setObjectName(QStringLiteral("contextPeekContent"));
    contentLayout = new QVBoxLayout(contentHost);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    rootLayout->addWidget(contentHost, 1);

    buildResizeHandles();

    connect(pinButton,
            &QToolButton::clicked,
            this,
            &ContextPeekHost::pinRequested);
    connect(fullViewButton,
            &QToolButton::clicked,
            this,
            &ContextPeekHost::fullViewRequested);
    connect(closeButton,
            &QToolButton::clicked,
            this,
            &ContextPeekHost::closeRequested);

    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape,
            &QShortcut::activated,
            this,
            [this]() {
                finishResize(false);
                emit closeRequested();
            });
}

void ContextPeekHost::buildResizeHandles()
{
    leftResizeHandle = new ContextPeekResizeHandle(
        HandleKind::Left, this);
    leftResizeHandle->setObjectName(
        QStringLiteral("contextPeekResizeLeft"));
    leftResizeHandle->setAccessibleName(
        tr("Resize context preview width"));
    leftResizeHandle->setToolTip(
        tr("Drag left or right to resize. Double-click to reset."));
    leftResizeHandle->setCursor(Qt::SizeHorCursor);

    bottomResizeHandle = new ContextPeekResizeHandle(
        HandleKind::Bottom, this);
    bottomResizeHandle->setObjectName(
        QStringLiteral("contextPeekResizeBottom"));
    bottomResizeHandle->setAccessibleName(
        tr("Resize context preview height"));
    bottomResizeHandle->setToolTip(
        tr("Drag upward or downward to resize. Double-click to reset."));
    bottomResizeHandle->setCursor(Qt::SizeVerCursor);

    cornerResizeHandle = new ContextPeekResizeHandle(
        HandleKind::Corner, this);
    cornerResizeHandle->setObjectName(
        QStringLiteral("contextPeekResizeCorner"));
    cornerResizeHandle->setAccessibleName(
        tr("Resize context preview width and height"));
    cornerResizeHandle->setToolTip(
        tr("Drag diagonally to resize. Double-click to reset."));
    cornerResizeHandle->setCursor(Qt::SizeBDiagCursor);

    for (QWidget* handle : {leftResizeHandle,
                            bottomResizeHandle,
                            cornerResizeHandle}) {
        handle->installEventFilter(this);
        handle->show();
        handle->raise();
    }
    updateResizeHandleGeometry();
}

void ContextPeekHost::synchronizeGeometry()
{
    QWidget* region = parentWidget();
    if (!region)
        return;
    const QRect available = region->contentsRect();
    if (available.isEmpty())
        return;
    const QSize visibleSize = boundedVisibleSize(preferredSizeValue);
    setGeometry(available.right() - visibleSize.width() + 1,
                available.bottom() - visibleSize.height() + 1,
                visibleSize.width(),
                visibleSize.height());
    updateResizeHandleGeometry();
}

void ContextPeekHost::updateResizeHandleGeometry()
{
    if (!rootLayout
        || !leftResizeHandle
        || !bottomResizeHandle
        || !cornerResizeHandle) {
        return;
    }
    const int thickness = qMin(
        resizeHandleThickness(this),
        qMax(1, qMin(width(), height())));
    const int cornerExtent = qMin(
        qMax(kMinimumCornerHitExtent, thickness * 2),
        qMax(1, qMin(width(), height())));
    rootLayout->setContentsMargins(
        thickness, 0, 0, thickness);

    const int leftHeight = qMax(0, height() - cornerExtent);
    leftResizeHandle->setGeometry(
        0, 0, thickness, leftHeight);
    const int bottomWidth = qMax(0, width() - cornerExtent);
    bottomResizeHandle->setGeometry(
        cornerExtent,
        qMax(0, height() - thickness),
        bottomWidth,
        thickness);
    cornerResizeHandle->setGeometry(
        0,
        qMax(0, height() - cornerExtent),
        cornerExtent,
        cornerExtent);
    leftResizeHandle->raise();
    bottomResizeHandle->raise();
    cornerResizeHandle->raise();
}

QSize ContextPeekHost::boundedStoredSize(
    const QSize& requested) const
{
    return QSize(
        ContextWorkspaceState::boundedPeekWidth(
            requested.width()),
        ContextWorkspaceState::boundedPeekHeight(
            requested.height()));
}

QSize ContextPeekHost::boundedVisibleSize(
    const QSize& requested) const
{
    QWidget* region = parentWidget();
    if (!region || region->contentsRect().isEmpty())
        return boundedStoredSize(requested);

    const QSize stored = boundedStoredSize(requested);
    const QSize available = region->contentsRect().size();
    const int minimumWidth = qMin(
        ContextWorkspaceState::kMinimumPeekWidth,
        available.width());
    const int reservedEditorWidth = qMin(
        kMinimumRemainingEditorWidth,
        qMax(0, available.width() - minimumWidth));
    const int maximumWidth = qMax(
        minimumWidth,
        qMin(ContextWorkspaceState::kMaximumPeekWidth,
             available.width() - reservedEditorWidth));
    const int minimumHeight = qMin(
        ContextWorkspaceState::kMinimumPeekHeight,
        available.height());
    return QSize(
        qBound(minimumWidth, stored.width(), maximumWidth),
        qBound(minimumHeight,
               stored.height(),
               available.height()));
}

ContextPeekHost::ResizeMode ContextPeekHost::modeForHandle(
    const QObject* object) const
{
    if (object == leftResizeHandle)
        return ResizeMode::Width;
    if (object == bottomResizeHandle)
        return ResizeMode::Height;
    if (object == cornerResizeHandle)
        return ResizeMode::WidthAndHeight;
    return ResizeMode::None;
}

bool ContextPeekHost::handleResizeEvent(
    QWidget* handle,
    QEvent* event)
{
    if (!handle || !event)
        return false;
    auto* resizeHandle =
        static_cast<ContextPeekResizeHandle*>(handle);
    auto* mouseEvent = dynamic_cast<QMouseEvent*>(event);
    if (event->type() == QEvent::MouseButtonDblClick
        && mouseEvent
        && mouseEvent->button() == Qt::LeftButton) {
        finishResize(false);
        mouseEvent->accept();
        emit preferredSizeResetRequested();
        return true;
    }
    if (event->type() == QEvent::MouseButtonPress
        && mouseEvent
        && mouseEvent->button() == Qt::LeftButton) {
        finishResize(false);
        resizeMode = modeForHandle(handle);
        resizeStartGlobal =
            mouseEvent->globalPosition().toPoint();
        resizeStartSize = size();
        resizeStartPreference = preferredSizeValue;
        resizeChanged = false;
        resizeHandle->setPressed(true);
        handle->grabMouse();
        mouseEvent->accept();
        return true;
    }
    if (event->type() == QEvent::MouseMove
        && mouseEvent
        && resizeMode != ResizeMode::None) {
        applyResizePosition(
            mouseEvent->globalPosition().toPoint());
        mouseEvent->accept();
        return true;
    }
    if (event->type() == QEvent::MouseButtonRelease
        && mouseEvent
        && mouseEvent->button() == Qt::LeftButton
        && resizeMode != ResizeMode::None) {
        applyResizePosition(
            mouseEvent->globalPosition().toPoint());
        mouseEvent->accept();
        finishResize(true);
        return true;
    }
    if (event->type() == QEvent::UngrabMouse
        && resizeMode != ResizeMode::None) {
        finishResize(isVisible());
        return false;
    }
    return false;
}

void ContextPeekHost::applyResizePosition(
    const QPoint& globalPosition)
{
    if (resizeMode == ResizeMode::None)
        return;
    const QPoint delta = globalPosition - resizeStartGlobal;
    QSize requested = resizeStartSize;
    const bool resizeWidth = resizeMode == ResizeMode::Width
        || resizeMode == ResizeMode::WidthAndHeight;
    const bool resizeHeight = resizeMode == ResizeMode::Height
        || resizeMode == ResizeMode::WidthAndHeight;
    if (resizeWidth)
        requested.setWidth(resizeStartSize.width() - delta.x());
    if (resizeHeight)
        requested.setHeight(resizeStartSize.height() - delta.y());
    const QSize visible = boundedVisibleSize(requested);
    QSize next = preferredSizeValue;
    if (resizeWidth)
        next.setWidth(visible.width());
    if (resizeHeight)
        next.setHeight(visible.height());
    next = boundedStoredSize(next);
    if (next == preferredSizeValue)
        return;
    preferredSizeValue = next;
    resizeChanged = true;
    synchronizeGeometry();
}

void ContextPeekHost::finishResize(bool commit)
{
    if (resizeMode == ResizeMode::None)
        return;
    const bool changed = resizeChanged;
    resizeMode = ResizeMode::None;
    resizeChanged = false;
    for (QWidget* handle : {leftResizeHandle,
                            bottomResizeHandle,
                            cornerResizeHandle}) {
        if (!handle)
            continue;
        static_cast<ContextPeekResizeHandle*>(handle)
            ->setPressed(false);
        if (QWidget::mouseGrabber() == handle)
            handle->releaseMouse();
    }
    if (!commit && changed) {
        preferredSizeValue = resizeStartPreference;
        synchronizeGeometry();
        return;
    }
    if (commit && changed)
        emit preferredSizeChanged(preferredSizeValue);
}
