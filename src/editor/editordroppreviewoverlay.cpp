#include "editordroppreviewoverlay.h"

#include "editorsplitcontroller.h"
#include "insightvisualstyle.h"

#include <QEvent>
#include <QFontMetrics>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QTabBar>
#include <QTabWidget>

EditorDropPreviewOverlay::EditorDropPreviewOverlay(
    QWidget* parent)
    : QWidget(parent)
    , previewDirection(EditorSplitDirection::Center)
{
    setObjectName(QStringLiteral("editorDropPreviewOverlay"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::NoFocus);
    hide();
}

EditorSplitDirection EditorDropPreviewOverlay::directionAt(
    const QRect& targetRect,
    const QPoint& targetPosition)
{
    if (!targetRect.isValid())
        return EditorSplitDirection::Center;

    const int horizontalEdge = qMin(
        90, qMax(24, targetRect.width() / 4));
    const int verticalEdge = qMin(
        90, qMax(24, targetRect.height() / 4));
    if (targetPosition.x()
        < targetRect.left() + horizontalEdge) {
        return EditorSplitDirection::Left;
    }
    if (targetPosition.x()
        >= targetRect.right() - horizontalEdge + 1) {
        return EditorSplitDirection::Right;
    }
    if (targetPosition.y()
        < targetRect.top() + verticalEdge) {
        return EditorSplitDirection::Above;
    }
    if (targetPosition.y()
        >= targetRect.bottom() - verticalEdge + 1) {
        return EditorSplitDirection::Below;
    }
    return EditorSplitDirection::Center;
}

QRect EditorDropPreviewOverlay::previewRect(
    const QRect& targetRect,
    EditorSplitDirection direction)
{
    if (!targetRect.isValid())
        return {};

    const int leftWidth = targetRect.width() / 2;
    const int topHeight = targetRect.height() / 2;
    switch (direction) {
    case EditorSplitDirection::Left:
        return QRect(
            targetRect.topLeft(),
            QSize(leftWidth, targetRect.height()));
    case EditorSplitDirection::Right:
        return QRect(
            targetRect.left() + leftWidth,
            targetRect.top(),
            targetRect.width() - leftWidth,
            targetRect.height());
    case EditorSplitDirection::Above:
        return QRect(
            targetRect.topLeft(),
            QSize(targetRect.width(), topHeight));
    case EditorSplitDirection::Below:
        return QRect(
            targetRect.left(),
            targetRect.top() + topHeight,
            targetRect.width(),
            targetRect.height() - topHeight);
    case EditorSplitDirection::Center:
        return targetRect;
    }
    return targetRect;
}

QPixmap EditorDropPreviewOverlay::tabDragPixmap(
    const QTabBar* tabBar,
    int tabIndex)
{
    if (!tabBar || tabIndex < 0
        || tabIndex >= tabBar->count()) {
        return {};
    }

    const QString label = tabBar->tabText(tabIndex);
    const QIcon icon = tabBar->tabIcon(tabIndex);
    const QFontMetrics metrics(tabBar->font());
    const int height = qMax(30, tabBar->tabRect(tabIndex).height());
    const int iconExtent = icon.isNull() ? 0 : qMin(18, height - 10);
    const int iconSpacing = iconExtent > 0 ? 6 : 0;
    const int horizontalPadding = 12;
    const int naturalWidth = horizontalPadding * 2
        + iconExtent + iconSpacing
        + metrics.horizontalAdvance(label);
    const int width = qBound(84, naturalWidth, 280);
    const qreal devicePixelRatio = tabBar->devicePixelRatioF();

    QPixmap pixmap(
        qRound(width * devicePixelRatio),
        qRound(height * devicePixelRatio));
    pixmap.setDevicePixelRatio(devicePixelRatio);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const InsightTheme& theme = InsightVisualStyle::theme();
    QColor background = theme.tab.tabBackgroundSelected;
    background.setAlpha(238);
    QPen border(theme.accent, 1.5);
    border.setCosmetic(true);
    painter.setPen(border);
    painter.setBrush(background);
    const QRectF backgroundRect = QRectF(0, 0, width, height)
                                      .adjusted(1, 1, -1, -1);
    painter.drawRoundedRect(backgroundRect, 5, 5);

    int textLeft = horizontalPadding;
    if (iconExtent > 0) {
        const QRect iconRect(
            horizontalPadding,
            (height - iconExtent) / 2,
            iconExtent,
            iconExtent);
        icon.paint(&painter, iconRect);
        textLeft = iconRect.right() + 1 + iconSpacing;
    }
    const QRect textRect(
        textLeft,
        0,
        qMax(1, width - textLeft - horizontalPadding),
        height);
    painter.setFont(tabBar->font());
    painter.setPen(theme.tab.textSelected);
    painter.drawText(
        textRect,
        Qt::AlignVCenter | Qt::AlignLeft,
        metrics.elidedText(
            label, Qt::ElideMiddle, textRect.width()));
    return pixmap;
}

void EditorDropPreviewOverlay::showPreview(
    QWidget* targetWidget,
    EditorSplitDirection directionValue)
{
    if (!targetWidget || !parentWidget()) {
        clearPreview();
        return;
    }

    if (previewTarget != targetWidget) {
        QObject::disconnect(targetDestroyedConnection);
        if (previewTarget)
            previewTarget->removeEventFilter(this);
        previewTarget = targetWidget;
        previewTarget->installEventFilter(this);
        targetDestroyedConnection = connect(
            previewTarget,
            &QObject::destroyed,
            this,
            [this]() {
                hide();
                previewTarget.clear();
                previewDirection = EditorSplitDirection::Center;
            });
    }
    previewDirection = directionValue;
    setProperty(
        "previewDirection",
        static_cast<int>(previewDirection));
    syncToTarget();
    if (!previewTarget || !previewTarget->isVisible()) {
        clearPreview();
        return;
    }
    raise();
    show();
    update();
}

void EditorDropPreviewOverlay::showPreview(
    QWidget* targetWidget,
    const QPoint& targetPosition)
{
    showPreview(
        targetWidget,
        targetWidget
            ? directionAt(targetWidget->rect(), targetPosition)
            : EditorSplitDirection::Center);
}

void EditorDropPreviewOverlay::clearPreview()
{
    hide();
    QObject::disconnect(targetDestroyedConnection);
    targetDestroyedConnection = {};
    if (previewTarget)
        previewTarget->removeEventFilter(this);
    previewTarget.clear();
    previewDirection = EditorSplitDirection::Center;
    setProperty(
        "previewDirection",
        static_cast<int>(previewDirection));
}

QWidget* EditorDropPreviewOverlay::target() const
{
    return previewTarget;
}

EditorSplitDirection EditorDropPreviewOverlay::direction() const
{
    return previewDirection;
}

QRect EditorDropPreviewOverlay::highlightedRect() const
{
    return previewRect(targetContentRect(), previewDirection);
}

bool EditorDropPreviewOverlay::eventFilter(
    QObject* watched,
    QEvent* event)
{
    if (watched != previewTarget)
        return QWidget::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::Move:
    case QEvent::Resize:
    case QEvent::Show:
    case QEvent::ParentChange:
        syncToTarget();
        break;
    case QEvent::Hide:
    case QEvent::Destroy:
        clearPreview();
        break;
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

void EditorDropPreviewOverlay::paintEvent(QPaintEvent*)
{
    const QRect highlight = highlightedRect();
    if (!highlight.isValid())
        return;

    const InsightTheme& theme = InsightVisualStyle::theme();
    QColor fill = theme.accent;
    fill.setAlpha(45);
    QColor borderColor = theme.accent;
    borderColor.setAlpha(225);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(fill);
    QPen border(borderColor, 2.0);
    border.setCosmetic(true);
    painter.setPen(border);
    painter.drawRoundedRect(
        QRectF(highlight).adjusted(1.5, 1.5, -1.5, -1.5),
        5,
        5);
}

void EditorDropPreviewOverlay::syncToTarget()
{
    QWidget* overlayParent = parentWidget();
    if (!overlayParent || !previewTarget)
        return;
    const QPoint topLeft = overlayParent->mapFromGlobal(
        previewTarget->mapToGlobal(QPoint(0, 0)));
    setGeometry(QRect(topLeft, previewTarget->size()));
}

QRect EditorDropPreviewOverlay::targetContentRect() const
{
    const auto* tabs = qobject_cast<QTabWidget*>(
        previewTarget.data());
    QWidget* page = tabs ? tabs->currentWidget() : nullptr;
    if (!page || !page->isVisible())
        return rect();

    const QPoint topLeft = mapFromGlobal(
        page->mapToGlobal(QPoint(0, 0)));
    const QRect pageRect(topLeft, page->size());
    const QRect bounded = pageRect.intersected(rect());
    return bounded.isValid() ? bounded : rect();
}
