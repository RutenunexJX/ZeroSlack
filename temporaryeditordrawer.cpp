#include "temporaryeditordrawer.h"

#include "editordroppreviewoverlay.h"
#include "editorsplitcontroller.h"
#include "insightvisualstyle.h"
#include "temporaryeditorsearchpopup.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCompleter>
#include <QEnterEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QSignalBlocker>
#include <QSizeGrip>
#include <QStyle>
#include <QPixmap>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <utility>

namespace {
constexpr int kMinimumExpandedWidth = 360;
constexpr int kMinimumExpandedHeight = 220;
constexpr int kHandleThickness = 30;
constexpr int kMaximumHandleLength = 220;
constexpr int kGripMargin = 3;
constexpr qreal kCornerRadius = 6.0;
constexpr int kPopupObservationArmMs = 750;

EditorSplitDirection splitDirectionForEdge(
    TemporaryEditorDrawer::Edge edge)
{
    switch (edge) {
    case TemporaryEditorDrawer::Edge::Left:
        return EditorSplitDirection::Left;
    case TemporaryEditorDrawer::Edge::Right:
        return EditorSplitDirection::Right;
    case TemporaryEditorDrawer::Edge::Top:
        return EditorSplitDirection::Above;
    case TemporaryEditorDrawer::Edge::Bottom:
        return EditorSplitDirection::Below;
    case TemporaryEditorDrawer::Edge::None:
        return EditorSplitDirection::Center;
    }
    return EditorSplitDirection::Center;
}

TemporaryEditorDrawer::Edge edgeForSplitDirection(
    EditorSplitDirection direction)
{
    switch (direction) {
    case EditorSplitDirection::Left:
        return TemporaryEditorDrawer::Edge::Left;
    case EditorSplitDirection::Right:
        return TemporaryEditorDrawer::Edge::Right;
    case EditorSplitDirection::Above:
        return TemporaryEditorDrawer::Edge::Top;
    case EditorSplitDirection::Below:
        return TemporaryEditorDrawer::Edge::Bottom;
    case EditorSplitDirection::Center:
        return TemporaryEditorDrawer::Edge::None;
    }
    return TemporaryEditorDrawer::Edge::None;
}

bool isVerticalEdge(TemporaryEditorDrawer::Edge edge)
{
    return edge == TemporaryEditorDrawer::Edge::Left
        || edge == TemporaryEditorDrawer::Edge::Right;
}

QPixmap tintedPixmap(const QPixmap& source, const QColor& color)
{
    if (source.isNull())
        return {};
    QPixmap tinted = source;
    QPainter painter(&tinted);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(tinted.rect(), color);
    return tinted;
}

QIcon themedStandardIcon(QStyle* style,
                         QStyle::StandardPixmap standardPixmap,
                         QWidget* owner,
                         const InsightTheme& theme)
{
    if (!style)
        return {};
    const QIcon source = style->standardIcon(
        standardPixmap, nullptr, owner);
    const QSize iconSize(16, 16);
    QIcon result;
    const QPixmap normal = tintedPixmap(
        source.pixmap(iconSize, QIcon::Normal),
        theme.button.text);
    const QPixmap disabled = tintedPixmap(
        source.pixmap(iconSize, QIcon::Disabled),
        theme.button.textDisabled);
    result.addPixmap(normal, QIcon::Normal, QIcon::Off);
    result.addPixmap(normal, QIcon::Normal, QIcon::On);
    result.addPixmap(disabled, QIcon::Disabled, QIcon::Off);
    result.addPixmap(disabled, QIcon::Disabled, QIcon::On);
    return result;
}
} // namespace

class DrawerEdgeHandle final : public QWidget
{
public:
    explicit DrawerEdgeHandle(QWidget* parent)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("temporaryEditorDrawerHandle"));
        setAttribute(Qt::WA_StyledBackground, false);
        setFocusPolicy(Qt::NoFocus);
        setMouseTracking(true);
    }

    void setEdge(TemporaryEditorDrawer::Edge nextEdge)
    {
        if (edge == nextEdge)
            return;
        edge = nextEdge;
        update();
    }

    void setText(const QString& nextText)
    {
        if (text == nextText)
            return;
        text = nextText;
        setToolTip(text);
        update();
    }

    void setHovered(bool nextHovered)
    {
        if (hovered == nextHovered)
            return;
        hovered = nextHovered;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        const InsightTheme& theme = InsightVisualStyle::theme();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QPen border(hovered ? theme.accent : theme.borderStrong, 1.0);
        border.setCosmetic(true);
        painter.setPen(border);
        painter.setBrush(hovered
                             ? QBrush(theme.button.backgroundHover)
                             : QBrush(theme.panelSubtle));
        painter.drawRoundedRect(
            QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
            kCornerRadius,
            kCornerRadius);

        painter.setPen(theme.textPrimary);
        painter.setFont(InsightVisualStyle::compactFont(font()));
        const QFontMetrics metrics(painter.font());
        const int padding = 8;
        if (isVerticalEdge(edge)) {
            painter.save();
            painter.translate(width() / 2.0, height() / 2.0);
            painter.rotate(edge == TemporaryEditorDrawer::Edge::Left
                               ? -90.0
                               : 90.0);
            const QRect textRect(
                -height() / 2 + padding,
                -width() / 2,
                qMax(1, height() - padding * 2),
                width());
            painter.drawText(
                textRect,
                Qt::AlignCenter,
                metrics.elidedText(
                    text, Qt::ElideMiddle, textRect.width()));
            painter.restore();
        } else {
            const QRect textRect = rect().adjusted(
                padding, 0, -padding, 0);
            painter.drawText(
                textRect,
                Qt::AlignCenter,
                metrics.elidedText(
                    text, Qt::ElideMiddle, textRect.width()));
        }
    }

private:
    TemporaryEditorDrawer::Edge edge =
        TemporaryEditorDrawer::Edge::None;
    QString text;
    bool hovered = false;
};

TemporaryEditorDrawer::TemporaryEditorDrawer(
    QWidget* editorRegion)
    : QWidget(editorRegion, Qt::Widget)
{
    Q_ASSERT(editorRegion);
    setObjectName(QStringLiteral("temporaryEditorDrawer"));
    setProperty("temporaryEditorOverlay", true);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setAttribute(Qt::WA_StyledBackground, false);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    buildChrome();

    previewOverlay = new EditorDropPreviewOverlay(editorRegion);
    previewOverlay->setObjectName(
        QStringLiteral("temporaryEditorDrawerDockPreview"));

    autoCollapseTimer = new QTimer(this);
    autoCollapseTimer->setSingleShot(true);
    autoCollapseTimer->setInterval(autoCollapseDelayValue);
    connect(autoCollapseTimer,
            &QTimer::timeout,
            this,
            [this]() {
                updateFocusWithin();
                if (computedInteractionActive()) {
                    scheduleAutoCollapse();
                    return;
                }
                collapseToHandle();
            });

    applicationEventArmTimer = new QTimer(this);
    applicationEventArmTimer->setSingleShot(true);
    applicationEventArmTimer->setInterval(kPopupObservationArmMs);
    connect(applicationEventArmTimer,
            &QTimer::timeout,
            this,
            [this]() {
                popupObservationArmed = false;
                updateApplicationEventFilterNeed();
            });

    // Geometry and chrome interaction are local concerns.  Keeping these
    // filters on their owning widgets avoids requiring an application-wide
    // filter while the drawer is hidden or represented only by its handle.
    editorRegion->installEventFilter(this);
    installLocalInteractionFilters(this);
    connect(qApp,
            &QGuiApplication::applicationStateChanged,
            this,
            [this](Qt::ApplicationState state) {
                if (state == Qt::ApplicationActive)
                    return;
                if (draggingTitleBar && titleBar)
                    titleBar->releaseMouse();
                if (resizingWithGrip && sizeGrip)
                    sizeGrip->releaseMouse();
                draggingTitleBar = false;
                resizingWithGrip = false;
                mousePressedValue = false;
                protectedPopups.clear();
                cancelApplicationEventObservation();
                clearDockPreview();
                updateInteractionState();
            });
    connect(&ApplicationThemeManager::instance(),
            &ApplicationThemeManager::themeChanged,
            this,
            [this](ThemeMode) {
                refreshTheme();
                update();
                if (previewOverlay)
                    previewOverlay->update();
            });

    refreshTheme();
    updateTitle();
    updateVisibleParts();
    hide();
}

TemporaryEditorDrawer::~TemporaryEditorDrawer()
{
    cancelApplicationEventObservation();
    if (previewOverlay) {
        previewOverlay->clearPreview();
        delete previewOverlay.data();
        previewOverlay = nullptr;
    }
}

TemporaryEditorDrawer::State TemporaryEditorDrawer::state() const
{
    return stateValue;
}

TemporaryEditorDrawer::Edge TemporaryEditorDrawer::edge() const
{
    return edgeValue;
}

bool TemporaryEditorDrawer::pinned() const
{
    return pinnedValue;
}

bool TemporaryEditorDrawer::isExpandedFromHandle() const
{
    return stateValue == State::EdgeStowed
        && expandedFromHandleValue;
}

bool TemporaryEditorDrawer::isHandleVisible() const
{
    return stateValue == State::EdgeStowed
        && !expandedFromHandleValue
        && isVisible()
        && edgeHandle
        && edgeHandle->isVisible();
}

bool TemporaryEditorDrawer::interactionActive() const
{
    return computedInteractionActive();
}

bool TemporaryEditorDrawer::dockPreviewVisible() const
{
    return previewOverlay && previewOverlay->isVisible();
}

TemporaryEditorDrawer::Edge
TemporaryEditorDrawer::dockPreviewEdge() const
{
    return dockPreviewVisible() ? previewEdgeValue : Edge::None;
}

EditorLocation TemporaryEditorDrawer::location() const
{
    return currentLocation;
}

QString TemporaryEditorDrawer::targetTitle() const
{
    return currentLocation.displayText();
}

QWidget* TemporaryEditorDrawer::editorWidget() const
{
    return editor.data();
}

QWidget* TemporaryEditorDrawer::handleWidget() const
{
    return edgeHandle;
}

QLineEdit* TemporaryEditorDrawer::searchField() const
{
    return searchEdit;
}

QToolButton* TemporaryEditorDrawer::backButton() const
{
    return backToolButton;
}

QToolButton* TemporaryEditorDrawer::forwardButton() const
{
    return forwardToolButton;
}

QToolButton* TemporaryEditorDrawer::pinButton() const
{
    return pinToolButton;
}

QToolButton* TemporaryEditorDrawer::closeButton() const
{
    return closeToolButton;
}

QRect TemporaryEditorDrawer::handleRect() const
{
    if (!parentWidget() || edgeValue == Edge::None)
        return {};
    if (isHandleVisible())
        return geometry();
    return collapsedHandleGeometry(edgeValue);
}

QRect TemporaryEditorDrawer::contentRect() const
{
    if (!parentWidget() || !contentHost || !contentHost->isVisible())
        return {};
    const QPoint topLeft = parentWidget()->mapFromGlobal(
        contentHost->mapToGlobal(QPoint(0, 0)));
    return QRect(topLeft, contentHost->size());
}

QRect TemporaryEditorDrawer::dockPreviewRect() const
{
    if (!previewOverlay || !previewOverlay->isVisible())
        return {};
    return previewOverlay->highlightedRect().translated(
        previewOverlay->pos());
}

QRect TemporaryEditorDrawer::floatingGeometry() const
{
    if (stateValue == State::Floating && isVisible())
        return geometry();
    return floatingGeometryValue;
}

QSize TemporaryEditorDrawer::preferredSize() const
{
    return preferredSizeValue;
}

int TemporaryEditorDrawer::autoCollapseDelayMs() const
{
    return autoCollapseDelayValue;
}

void TemporaryEditorDrawer::setAutoCollapseDelayMs(
    int milliseconds)
{
    const int nextDelay = qMax(0, milliseconds);
    if (autoCollapseDelayValue == nextDelay)
        return;
    autoCollapseDelayValue = nextDelay;
    if (autoCollapseTimer)
        autoCollapseTimer->setInterval(nextDelay);
}

void TemporaryEditorDrawer::setExternalInteractionActive(
    bool active)
{
    if (externalInteractionValue == active)
        return;
    externalInteractionValue = active;
    updateInteractionState();
}

void TemporaryEditorDrawer::setLocation(
    const EditorLocation& nextLocation)
{
    if (currentLocation == nextLocation)
        return;
    currentLocation = nextLocation;
    updateTitle();
    emit locationChanged(currentLocation);
}

void TemporaryEditorDrawer::setEditorWidget(QWidget* nextEditor)
{
    if (editor == nextEditor)
        return;

    if (editor) {
        removeLocalInteractionFilters(editor);
        contentLayout->removeWidget(editor);
        editor->hide();
    }

    editor = nextEditor;
    if (editor) {
        editor->setParent(contentHost);
        editor->setSizePolicy(
            QSizePolicy::Expanding, QSizePolicy::Expanding);
        contentLayout->addWidget(editor);
        installLocalInteractionFilters(editor);
        discoverCompleterPopups();
    }
    updateVisibleParts();
    emit editorWidgetChanged(editor.data());
}

QWidget* TemporaryEditorDrawer::takeEditorWidget()
{
    QWidget* detached = editor.data();
    if (!detached)
        return nullptr;
    removeLocalInteractionFilters(detached);
    contentLayout->removeWidget(detached);
    detached->hide();
    if (parentWidget())
        detached->setParent(parentWidget());
    editor.clear();
    emit editorWidgetChanged(nullptr);
    return detached;
}

void TemporaryEditorDrawer::setNavigationAvailability(
    bool canGoBack,
    bool canGoForward)
{
    backToolButton->setEnabled(canGoBack);
    forwardToolButton->setEnabled(canGoForward);
}

void TemporaryEditorDrawer::setState(State nextState)
{
    if (nextState == State::EdgeStowed) {
        stow(edgeValue == Edge::None ? Edge::Right : edgeValue);
        return;
    }

    if (stateValue == nextState) {
        synchronizeGeometryWithRegion();
        return;
    }

    if (nextState == State::Hidden) {
        if (stateValue == State::Floating && isVisible()) {
            floatingGeometryValue = geometry();
            preferredSizeValue = geometry().size();
        } else if (stateValue == State::EdgeStowed
                   && expandedFromHandleValue) {
            preferredSizeValue = geometry().size();
        }
        stateValue = State::Hidden;
        cancelAutoCollapse();
        clearDockPreview();
        hoverWithinValue = false;
        mousePressedValue = false;
        if (expandedFromHandleValue) {
            expandedFromHandleValue = false;
            emit expandedFromHandleChanged(false);
        }
        updateVisibleParts();
        hide();
        updateInteractionState();
        emit stateChanged(stateValue);
        return;
    }

    if (stateValue == State::EdgeStowed
        && expandedFromHandleValue) {
        floatingGeometryValue = geometry();
    }
    stateValue = State::Floating;
    if (edgeValue != Edge::None) {
        edgeValue = Edge::None;
        emit edgeChanged(edgeValue);
    }
    if (expandedFromHandleValue) {
        expandedFromHandleValue = false;
        emit expandedFromHandleChanged(false);
    }
    const QRect nextGeometry =
        boundedFloatingGeometry(floatingGeometryValue);
    updateVisibleParts();
    setGeometry(nextGeometry);
    floatingGeometryValue = nextGeometry;
    show();
    raise();
    emit stateChanged(stateValue);
    emit floatingGeometryChanged(floatingGeometryValue);
}

void TemporaryEditorDrawer::setEdge(Edge nextEdge)
{
    if (nextEdge == edgeValue)
        return;

    if (stateValue == State::Hidden) {
        edgeValue = nextEdge;
        edgeHandle->setEdge(edgeValue);
        emit edgeChanged(edgeValue);
        emit geometryPreferenceChanged(
            preferredSizeValue, edgeValue);
        return;
    }

    if (nextEdge == Edge::None) {
        setState(State::Floating);
        return;
    }
    stow(nextEdge);
}

void TemporaryEditorDrawer::setPinned(bool nextPinned)
{
    if (pinnedValue == nextPinned)
        return;
    pinnedValue = nextPinned;
    updatePinPresentation();
    if (pinnedValue && stateValue == State::EdgeStowed
        && !expandedFromHandleValue) {
        expandFromHandle();
    }
    if (pinnedValue)
        cancelAutoCollapse();
    else
        scheduleAutoCollapse();
    emit pinnedChanged(pinnedValue);
    emit pinChanged(pinnedValue);
}

void TemporaryEditorDrawer::setFloatingGeometry(
    const QRect& nextGeometry)
{
    if (!nextGeometry.isValid())
        return;
    const QRect bounded = boundedFloatingGeometry(nextGeometry);
    const QSize nextSize = bounded.size();
    const bool sizeChanged = preferredSizeValue != nextSize;
    const bool geometryChanged = floatingGeometryValue != bounded;
    preferredSizeValue = nextSize;
    floatingGeometryValue = bounded;
    if (stateValue == State::Floating)
        setGeometry(bounded);
    if (sizeChanged)
        emit preferredSizeChanged(preferredSizeValue);
    if (geometryChanged)
        emit floatingGeometryChanged(floatingGeometryValue);
    if (sizeChanged || geometryChanged) {
        emit geometryPreferenceChanged(
            preferredSizeValue, edgeValue);
    }
}

void TemporaryEditorDrawer::setPreferredSize(const QSize& nextSize)
{
    const QSize bounded = boundedPreferredSize(nextSize);
    if (preferredSizeValue == bounded)
        return;
    preferredSizeValue = bounded;
    if (stateValue == State::Floating) {
        QRect next = geometry();
        next.setSize(preferredSizeValue);
        next = boundedFloatingGeometry(next);
        setGeometry(next);
        floatingGeometryValue = next;
        emit floatingGeometryChanged(floatingGeometryValue);
    } else if (stateValue == State::EdgeStowed
               && expandedFromHandleValue) {
        setGeometry(expandedGeometryForEdge(edgeValue));
    }
    emit preferredSizeChanged(preferredSizeValue);
    emit geometryPreferenceChanged(
        preferredSizeValue, edgeValue);
}

void TemporaryEditorDrawer::setSearchCandidates(
    const EditorSearchCandidates& candidates,
    const QString& query)
{
    if (searchPopup)
        searchPopup->setCandidates(candidates, query);
}

void TemporaryEditorDrawer::open(
    const EditorLocation& nextLocation)
{
    setLocation(nextLocation);
    if (stateValue == State::Hidden) {
        if (edgeValue == Edge::None)
            setState(State::Floating);
        else {
            stow(edgeValue);
            expandFromHandle();
        }
    } else if (stateValue == State::EdgeStowed) {
        expandFromHandle();
    }
    show();
    raise();
    emit opened(currentLocation);
}

void TemporaryEditorDrawer::requestOpen(
    const EditorLocation& requestedLocation)
{
    emit openRequested(requestedLocation);
}

void TemporaryEditorDrawer::requestBack()
{
    if (backToolButton->isEnabled())
        emit backRequested();
}

void TemporaryEditorDrawer::requestForward()
{
    if (forwardToolButton->isEnabled())
        emit forwardRequested();
}

void TemporaryEditorDrawer::closeDrawer()
{
    if (stateValue == State::Hidden)
        return;
    setState(State::Hidden);
    emit closeRequested();
}

void TemporaryEditorDrawer::stow(Edge nextEdge)
{
    if (nextEdge == Edge::None) {
        setState(State::Floating);
        return;
    }

    if (stateValue == State::Floating && isVisible()) {
        floatingGeometryValue = geometry();
        preferredSizeValue = geometry().size();
    } else if (stateValue == State::EdgeStowed
               && expandedFromHandleValue) {
        preferredSizeValue = geometry().size();
    }

    const bool stateWasDifferent = stateValue != State::EdgeStowed;
    const bool edgeWasDifferent = edgeValue != nextEdge;
    stateValue = State::EdgeStowed;
    edgeValue = nextEdge;
    edgeHandle->setEdge(edgeValue);
    if (expandedFromHandleValue) {
        expandedFromHandleValue = false;
        emit expandedFromHandleChanged(false);
    }
    clearDockPreview();
    updateVisibleParts();
    show();
    setGeometry(collapsedHandleGeometry(edgeValue));
    raise();
    if (edgeWasDifferent)
        emit edgeChanged(edgeValue);
    if (stateWasDifferent)
        emit stateChanged(stateValue);
    emit geometryPreferenceChanged(
        preferredSizeValue, edgeValue);
    updateInteractionState();
}

void TemporaryEditorDrawer::expandFromHandle()
{
    if (stateValue != State::EdgeStowed
        || edgeValue == Edge::None)
        return;
    if (!expandedFromHandleValue) {
        expandedFromHandleValue = true;
        updateVisibleParts();
        setGeometry(expandedGeometryForEdge(edgeValue));
        show();
        raise();
        emit expandedFromHandleChanged(true);
    }
    cancelAutoCollapse();
    updateInteractionState();
}

void TemporaryEditorDrawer::collapseToHandle()
{
    if (stateValue != State::EdgeStowed
        || !expandedFromHandleValue)
        return;
    cancelAutoCollapse();
    preferredSizeValue = geometry().size();
    expandedFromHandleValue = false;
    hoverWithinValue = false;
    updateVisibleParts();
    setGeometry(collapsedHandleGeometry(edgeValue));
    show();
    raise();
    emit expandedFromHandleChanged(false);
    emit geometryPreferenceChanged(
        preferredSizeValue, edgeValue);
    updateInteractionState();
}

void TemporaryEditorDrawer::showDockPreview(Edge nextEdge)
{
    if (nextEdge == Edge::None || !previewOverlay
        || !parentWidget()) {
        clearDockPreview();
        return;
    }
    previewEdgeValue = nextEdge;
    previewOverlay->showPreview(
        parentWidget(), splitDirectionForEdge(nextEdge));
    raise();
    emit dockPreviewChanged(
        dockPreviewVisible(),
        dockPreviewEdge(),
        dockPreviewRect());
}

void TemporaryEditorDrawer::clearDockPreview()
{
    const bool wasVisible = dockPreviewVisible();
    const Edge previousEdge = previewEdgeValue;
    if (previewOverlay)
        previewOverlay->clearPreview();
    previewEdgeValue = Edge::None;
    if (wasVisible || previousEdge != Edge::None) {
        emit dockPreviewChanged(false, Edge::None, QRect());
    }
}

void TemporaryEditorDrawer::refreshTheme()
{
    if (applyingTheme)
        return;
    applyingTheme = true;

    const InsightTheme& theme = InsightVisualStyle::theme();
    QPalette drawerPalette = palette();
    drawerPalette.setColor(QPalette::Window, theme.panelBackground);
    drawerPalette.setColor(QPalette::WindowText, theme.textPrimary);
    drawerPalette.setColor(QPalette::Base, theme.input.background);
    drawerPalette.setColor(QPalette::Text, theme.input.text);
    drawerPalette.setColor(QPalette::Button, theme.button.background);
    drawerPalette.setColor(QPalette::ButtonText, theme.button.text);
    drawerPalette.setColor(
        QPalette::Disabled,
        QPalette::WindowText,
        theme.textMuted);
    drawerPalette.setColor(
        QPalette::Disabled,
        QPalette::ButtonText,
        theme.button.textDisabled);
    setPalette(drawerPalette);

    QPalette titlePalette = drawerPalette;
    titlePalette.setColor(QPalette::Window, theme.panelSubtle);
    titlePalette.setColor(QPalette::WindowText, theme.textPrimary);
    titleBar->setAutoFillBackground(true);
    titleBar->setPalette(titlePalette);
    titleBar->setStyleSheet(
        InsightVisualStyle::packageToolsBarStyleSheet(
            titleBar->objectName()));
    contentHost->setPalette(drawerPalette);
    titleLabel->setPalette(titlePalette);
    titleLabel->setFont(
        InsightVisualStyle::labelFont(titleLabel->font()));
    titleLabel->setStyleSheet(
        InsightVisualStyle::labelStyleSheet(
            titleLabel->objectName(), true));
    QPalette searchPalette = drawerPalette;
    searchPalette.setColor(QPalette::Base, theme.input.background);
    searchPalette.setColor(QPalette::Text, theme.input.text);
    searchPalette.setColor(
        QPalette::PlaceholderText, theme.textMuted);
    searchPalette.setColor(
        QPalette::Disabled, QPalette::Text, theme.button.textDisabled);
    searchEdit->setPalette(searchPalette);
    InsightVisualStyle::applySearchField(searchEdit);
    if (searchPopup)
        searchPopup->refreshTheme();

    backToolButton->setIcon(themedStandardIcon(
        style(), QStyle::SP_ArrowBack, this, theme));
    forwardToolButton->setIcon(themedStandardIcon(
        style(), QStyle::SP_ArrowForward, this, theme));
    closeToolButton->setIcon(themedStandardIcon(
        style(), QStyle::SP_DialogCloseButton, this, theme));
    updatePinPresentation();

    edgeHandle->setFont(
        InsightVisualStyle::compactFont(font()));
    edgeHandle->setPalette(drawerPalette);
    edgeHandle->update();
    if (previewOverlay)
        previewOverlay->update();
    update();
    applyingTheme = false;
}

bool TemporaryEditorDrawer::eventFilter(
    QObject* watched,
    QEvent* event)
{
    if (!event)
        return QWidget::eventFilter(watched, event);

    if (watched == parentWidget()
        && event->type() == QEvent::Resize) {
        synchronizeGeometryWithRegion();
    }

    const QEvent::Type type = event->type();

    // Title dragging, grip resizing, and handle hover are object-local.  Gate
    // their high-frequency event types by pointer identity before any cast.
    if (type == QEvent::MouseMove) {
        if (watched == sizeGrip && handleSizeGripEvent(event))
            return true;
        if (isTitleDragSurface(watched)
            && handleTitleBarEvent(event)) {
            return true;
        }
        return QWidget::eventFilter(watched, event);
    }
    if (type == QEvent::Enter || type == QEvent::Leave) {
        if (watched != edgeHandle)
            return QWidget::eventFilter(watched, event);
        if (type == QEvent::Enter) {
            edgeHandle->setHovered(true);
            hoverWithinValue = true;
            cancelAutoCollapse();
            expandFromHandle();
            updateInteractionState();
        } else if (!expandedFromHandleValue) {
            edgeHandle->setHovered(false);
            hoverWithinValue = false;
            updateInteractionState();
        }
        return QWidget::eventFilter(watched, event);
    }

    if (type == QEvent::FocusIn || type == QEvent::FocusOut) {
        if (objectBelongsToDrawer(watched)) {
            ++focusOwnershipEvaluationCount;
            if (type == QEvent::FocusIn) {
                focusWithinValue = true;
                updateInteractionState();
            } else {
                QTimer::singleShot(0, this, [this]() {
                    updateFocusWithin();
                });
            }
        }
        return QWidget::eventFilter(watched, event);
    }

    // Only title dragging and grip resizing need an application-level release
    // guard.  Ordinary editor/chrome clicks retain their widget-local delivery
    // and never arm global observation.
    if (type == QEvent::MouseButtonPress
        && objectBelongsToDrawer(watched)
        && (watched == sizeGrip
            || isTitleDragSurface(watched))) {
        auto* pressEvent = dynamic_cast<QMouseEvent*>(event);
        if (!pressEvent
            || pressEvent->button() != Qt::LeftButton) {
            return QWidget::eventFilter(watched, event);
        }
        releaseObservationArmed = true;
        mousePressedValue = true;
        cancelAutoCollapse();
        updateApplicationEventFilterNeed();
        updateInteractionState();
        if (watched == sizeGrip
            && handleSizeGripEvent(event)) {
            return true;
        }
        if (isTitleDragSurface(watched)
            && handleTitleBarEvent(event)) {
            return true;
        }
        return QWidget::eventFilter(watched, event);
    }

    // Completer popups have a direct object-local lifecycle filter.  Ordinary
    // input only discovers that known popup and never installs qApp filtering.
    if ((type == QEvent::KeyPress
         || type == QEvent::InputMethod)
        && objectBelongsToDrawer(watched)) {
        discoverCompleterPopups();
        return QWidget::eventFilter(watched, event);
    }

    // A context-menu event is the sole short application-level popup arm;
    // the following Show still has to resolve to drawer ownership.
    if (type == QEvent::ContextMenu
        && objectBelongsToDrawer(watched)) {
        armPopupObservation();
        return QWidget::eventFilter(watched, event);
    }

    // While armed, only popup lifecycle, the matching release, and app
    // deactivation reach the application-level branch.  Everything else
    // returns before QWidget conversion or parent-chain traversal.
    switch (type) {
    case QEvent::Show:
    case QEvent::Hide:
    case QEvent::Close:
    case QEvent::Destroy:
    case QEvent::MouseButtonRelease:
    case QEvent::ApplicationDeactivate:
        break;
    default:
        return QWidget::eventFilter(watched, event);
    }

    if (applicationEventFilterInstalled)
        ++applicationEventDispatchCount;

    QWidget* watchedWidget = qobject_cast<QWidget*>(watched);
    if (watchedWidget && isPopupLike(watchedWidget)) {
        if (type == QEvent::Show) {
            const bool contentVisible = isVisible()
                && (stateValue == State::Floating
                    || (stateValue == State::EdgeStowed
                        && expandedFromHandleValue));
            const bool shouldProtect = contentVisible
                && (((popupObservationArmed
                      || !protectedPopups.isEmpty())
                     && objectBelongsToDrawer(watched))
                    || isLocalCompleterPopup(watchedWidget));
            if (shouldProtect)
                trackPopup(watchedWidget, true);
            if (shouldProtect) {
                popupObservationArmed = false;
                if (applicationEventArmTimer)
                    applicationEventArmTimer->stop();
                updateApplicationEventFilterNeed();
            }
        } else if ((type == QEvent::Hide
                    || type == QEvent::Close
                    || type == QEvent::Destroy)
                   && isProtectedPopup(watchedWidget)) {
            trackPopup(watchedWidget, false);
            updateApplicationEventFilterNeed();
        }
    }

    if (type == QEvent::MouseButtonRelease) {
        bool handledRelease = false;
        auto* mouseEvent = dynamic_cast<QMouseEvent*>(event);
        if (mouseEvent && draggingTitleBar) {
            finishTitleDrag(mouseEvent->globalPosition().toPoint());
            handledRelease = true;
        }
        if (resizingWithGrip) {
            finishGripResize();
            handledRelease = true;
        }
        mousePressedValue = false;
        releaseObservationArmed = false;
        updateInteractionState();
        updateApplicationEventFilterNeed();
        if (handledRelease)
            return true;
    }

    if (type == QEvent::ApplicationDeactivate) {
        mousePressedValue = false;
        draggingTitleBar = false;
        resizingWithGrip = false;
        protectedPopups.clear();
        cancelApplicationEventObservation();
        clearDockPreview();
        updateInteractionState();
    }

    return QWidget::eventFilter(watched, event);
}

void TemporaryEditorDrawer::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (!event)
        return;
    switch (event->type()) {
    case QEvent::PaletteChange:
    case QEvent::ApplicationPaletteChange:
    case QEvent::StyleChange:
        refreshTheme();
        break;
    default:
        break;
    }
}

void TemporaryEditorDrawer::enterEvent(QEnterEvent* event)
{
    hoverWithinValue = true;
    cancelAutoCollapse();
    updateInteractionState();
    QWidget::enterEvent(event);
}

void TemporaryEditorDrawer::focusInEvent(QFocusEvent* event)
{
    ++focusOwnershipEvaluationCount;
    focusWithinValue = true;
    updateInteractionState();
    QWidget::focusInEvent(event);
}

void TemporaryEditorDrawer::focusOutEvent(QFocusEvent* event)
{
    ++focusOwnershipEvaluationCount;
    QTimer::singleShot(0, this, [this]() {
        updateFocusWithin();
    });
    QWidget::focusOutEvent(event);
}

void TemporaryEditorDrawer::hideEvent(QHideEvent* event)
{
    cancelApplicationEventObservation();
    QWidget::hideEvent(event);
}

void TemporaryEditorDrawer::leaveEvent(QEvent* event)
{
    hoverWithinValue = false;
    edgeHandle->setHovered(false);
    updateInteractionState();
    QWidget::leaveEvent(event);
}

void TemporaryEditorDrawer::paintEvent(QPaintEvent*)
{
    const InsightTheme& theme = InsightVisualStyle::theme();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen border(theme.borderStrong, 1.0);
    border.setCosmetic(true);
    painter.setPen(border);
    painter.setBrush(theme.panelBackground);
    painter.drawRoundedRect(
        QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
        kCornerRadius,
        kCornerRadius);
}

void TemporaryEditorDrawer::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (edgeHandle && edgeHandle->isVisible())
        edgeHandle->setGeometry(rect());
    if (searchPopup)
        searchPopup->synchronizeGeometry();
    updateGripGeometry();
}

void TemporaryEditorDrawer::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    updateApplicationEventFilterLifecycle();
    synchronizeGeometryWithRegion();
    raise();
}

void TemporaryEditorDrawer::buildChrome()
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(1, 1, 1, 1);
    outerLayout->setSpacing(0);

    chrome = new QWidget(this);
    chrome->setObjectName(
        QStringLiteral("temporaryEditorDrawerChrome"));
    auto* chromeLayout = new QVBoxLayout(chrome);
    chromeLayout->setContentsMargins(0, 0, 0, 0);
    chromeLayout->setSpacing(0);

    titleBar = new QWidget(chrome);
    titleBar->setObjectName(
        QStringLiteral("temporaryEditorDrawerTitleBar"));
    titleBar->setCursor(Qt::SizeAllCursor);
    auto* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(7, 5, 7, 5);
    titleLayout->setSpacing(5);

    backToolButton = new QToolButton(titleBar);
    backToolButton->setObjectName(
        QStringLiteral("temporaryEditorDrawerBackButton"));
    backToolButton->setToolTip(
        QStringLiteral("Back in temporary editor"));
    backToolButton->setAutoRaise(true);
    backToolButton->setFocusPolicy(Qt::NoFocus);
    backToolButton->setEnabled(false);
    titleLayout->addWidget(backToolButton);

    forwardToolButton = new QToolButton(titleBar);
    forwardToolButton->setObjectName(
        QStringLiteral("temporaryEditorDrawerForwardButton"));
    forwardToolButton->setToolTip(
        QStringLiteral("Forward in temporary editor"));
    forwardToolButton->setAutoRaise(true);
    forwardToolButton->setFocusPolicy(Qt::NoFocus);
    forwardToolButton->setEnabled(false);
    titleLayout->addWidget(forwardToolButton);

    titleLabel = new QLabel(titleBar);
    titleLabel->setObjectName(
        QStringLiteral("temporaryEditorDrawerTargetTitle"));
    titleLabel->setTextFormat(Qt::PlainText);
    titleLabel->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Preferred);
    titleLabel->setCursor(Qt::SizeAllCursor);
    titleLayout->addWidget(titleLabel, 1);

    searchEdit = new QLineEdit(titleBar);
    searchEdit->setObjectName(
        QStringLiteral("temporaryEditorDrawerTargetSearch"));
    searchEdit->setClearButtonEnabled(true);
    searchEdit->setPlaceholderText(
        QStringLiteral("Open file, module, package, or symbol"));
    titleLayout->addWidget(searchEdit);

    pinToolButton = new QToolButton(titleBar);
    pinToolButton->setObjectName(
        QStringLiteral("temporaryEditorDrawerPinButton"));
    pinToolButton->setCheckable(true);
    pinToolButton->setAutoRaise(true);
    pinToolButton->setFocusPolicy(Qt::NoFocus);
    titleLayout->addWidget(pinToolButton);

    closeToolButton = new QToolButton(titleBar);
    closeToolButton->setObjectName(
        QStringLiteral("temporaryEditorDrawerCloseButton"));
    closeToolButton->setToolTip(
        QStringLiteral("Close temporary editor"));
    closeToolButton->setAutoRaise(true);
    closeToolButton->setFocusPolicy(Qt::NoFocus);
    titleLayout->addWidget(closeToolButton);
    chromeLayout->addWidget(titleBar);

    contentHost = new QWidget(chrome);
    contentHost->setObjectName(
        QStringLiteral("temporaryEditorDrawerContent"));
    contentHost->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Expanding);
    contentLayout = new QVBoxLayout(contentHost);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    chromeLayout->addWidget(contentHost, 1);

    outerLayout->addWidget(chrome);

    edgeHandle = new DrawerEdgeHandle(this);
    edgeHandle->raise();

    sizeGrip = new QSizeGrip(this);
    sizeGrip->setObjectName(
        QStringLiteral("temporaryEditorDrawerSizeGrip"));
    sizeGrip->raise();

    searchPopup = new TemporaryEditorSearchPopup(this);
    searchPopup->attachSearchField(searchEdit);
    searchPopup->setActivationHandler(
        [this](const EditorSearchCandidate& candidate) {
            const QSignalBlocker blocker(searchEdit);
            searchEdit->clear();
            emit searchCandidateActivated(candidate);
        });

    connect(backToolButton,
            &QToolButton::clicked,
            this,
            &TemporaryEditorDrawer::requestBack);
    connect(forwardToolButton,
            &QToolButton::clicked,
            this,
            &TemporaryEditorDrawer::requestForward);
    connect(pinToolButton,
            &QToolButton::toggled,
            this,
            &TemporaryEditorDrawer::setPinned);
    connect(closeToolButton,
            &QToolButton::clicked,
            this,
            &TemporaryEditorDrawer::closeDrawer);
    connect(searchEdit,
            &QLineEdit::textChanged,
            this,
            &TemporaryEditorDrawer::searchTextChanged);
    connect(searchEdit,
            &QLineEdit::textChanged,
            this,
            [this](const QString&) {
                discoverCompleterPopups();
            });
}

void TemporaryEditorDrawer::updateTitle()
{
    const QString text = targetTitle();
    titleLabel->setText(text);
    titleLabel->setToolTip(text);
    edgeHandle->setText(text);
}

void TemporaryEditorDrawer::updatePinPresentation()
{
    if (!pinToolButton)
        return;
    const QSignalBlocker blocker(pinToolButton);
    pinToolButton->setChecked(pinnedValue);
    pinToolButton->setToolTip(
        pinnedValue
            ? QStringLiteral("Unpin automatic collapse")
            : QStringLiteral("Pin temporary editor open"));
    pinToolButton->setIcon(
        themedStandardIcon(
            style(),
            pinnedValue
                ? QStyle::SP_TitleBarUnshadeButton
                : QStyle::SP_TitleBarShadeButton,
            this,
            InsightVisualStyle::theme()));
}

void TemporaryEditorDrawer::updateGripGeometry()
{
    if (!sizeGrip)
        return;
    const QSize gripSize = sizeGrip->sizeHint().expandedTo(
        QSize(16, 16));
    sizeGrip->setGeometry(
        qMax(0, width() - gripSize.width() - kGripMargin),
        qMax(0, height() - gripSize.height() - kGripMargin),
        gripSize.width(),
        gripSize.height());
    sizeGrip->raise();
}

void TemporaryEditorDrawer::updateVisibleParts()
{
    const bool contentVisible = stateValue == State::Floating
        || (stateValue == State::EdgeStowed
            && expandedFromHandleValue);
    const bool handleVisible = stateValue == State::EdgeStowed
        && !expandedFromHandleValue;
    chrome->setVisible(contentVisible);
    edgeHandle->setVisible(handleVisible);
    sizeGrip->setVisible(contentVisible);
    if (editor)
        editor->setVisible(contentVisible);
    if (!contentVisible && searchPopup)
        searchPopup->clearCandidates();
    if (handleVisible) {
        edgeHandle->setEdge(edgeValue);
        edgeHandle->setGeometry(rect());
        edgeHandle->raise();
    }
    if (contentVisible)
        sizeGrip->raise();
    updateApplicationEventFilterLifecycle();
}

void TemporaryEditorDrawer::updateApplicationEventFilterLifecycle()
{
    const bool contentVisible = stateValue == State::Floating
        || (stateValue == State::EdgeStowed
            && expandedFromHandleValue);
    if (!contentVisible || !isVisible()) {
        cancelApplicationEventObservation();
        return;
    }
    updateApplicationEventFilterNeed();
}

void TemporaryEditorDrawer::setApplicationEventFilterEnabled(
    bool enabled)
{
    if (!qApp || applicationEventFilterInstalled == enabled)
        return;
    if (enabled)
        qApp->installEventFilter(this);
    else
        qApp->removeEventFilter(this);
    applicationEventFilterInstalled = enabled;
}

void TemporaryEditorDrawer::installLocalInteractionFilters(
    QWidget* root)
{
    if (!root)
        return;
    if (root != this)
        root->installEventFilter(this);
    const QList<QWidget*> descendants =
        root->findChildren<QWidget*>();
    for (QWidget* descendant : descendants) {
        if (descendant && descendant != this)
            descendant->installEventFilter(this);
    }
}

void TemporaryEditorDrawer::removeLocalInteractionFilters(
    QWidget* root)
{
    if (!root)
        return;
    if (root != this)
        root->removeEventFilter(this);
    const QList<QWidget*> descendants =
        root->findChildren<QWidget*>();
    for (QWidget* descendant : descendants) {
        if (descendant && descendant != this)
            descendant->removeEventFilter(this);
    }
    const QList<QCompleter*> completers =
        root->findChildren<QCompleter*>();
    for (QCompleter* completer : completers) {
        QWidget* popup = completer
            ? completer->popup()
            : nullptr;
        if (popup) {
            popup->removeEventFilter(this);
            trackPopup(popup, false);
            const auto connection =
                popupDestroyedConnections.take(popup);
            if (connection)
                QObject::disconnect(connection);
            if (popup->isVisible())
                popup->hide();
        }
        for (qsizetype index = localCompleterPopups.size() - 1;
             index >= 0;
             --index) {
            if (!localCompleterPopups.at(index)
                || localCompleterPopups.at(index) == popup) {
                localCompleterPopups.removeAt(index);
            }
        }
    }
    updateApplicationEventFilterNeed();
}

void TemporaryEditorDrawer::armPopupObservation()
{
    const bool contentVisible = stateValue == State::Floating
        || (stateValue == State::EdgeStowed
            && expandedFromHandleValue);
    if (!contentVisible || !isVisible())
        return;

    popupObservationArmed = true;
    applicationEventArmTimer->start();
    updateApplicationEventFilterNeed();
}

void TemporaryEditorDrawer::cancelApplicationEventObservation()
{
    if (applicationEventArmTimer)
        applicationEventArmTimer->stop();
    popupObservationArmed = false;
    releaseObservationArmed = false;
    protectedPopups.clear();
    setApplicationEventFilterEnabled(false);
}

void TemporaryEditorDrawer::updateApplicationEventFilterNeed()
{
    pruneProtectedPopups();
    bool hasProtectedPopup = false;
    for (const QPointer<QWidget>& popup : protectedPopups) {
        if (popup) {
            hasProtectedPopup = true;
            break;
        }
    }
    const bool contentVisible = isVisible()
        && (stateValue == State::Floating
            || (stateValue == State::EdgeStowed
                && expandedFromHandleValue));
    setApplicationEventFilterEnabled(
        contentVisible
        && (popupObservationArmed
            || releaseObservationArmed
            || hasProtectedPopup));
}

void TemporaryEditorDrawer::discoverCompleterPopups()
{
    for (qsizetype index = localCompleterPopups.size() - 1;
         index >= 0;
         --index) {
        if (!localCompleterPopups.at(index))
            localCompleterPopups.removeAt(index);
    }
    QList<QCompleter*> completers;
    if (searchEdit && searchEdit->completer())
        completers.append(searchEdit->completer());
    if (editor) {
        const QList<QCompleter*> editorCompleters =
            editor->findChildren<QCompleter*>();
        for (QCompleter* completer : editorCompleters) {
            if (completer && !completers.contains(completer))
                completers.append(completer);
        }
    }
    for (QCompleter* completer : completers) {
        QWidget* popup = completer
            ? completer->popup()
            : nullptr;
        if (!popup || isLocalCompleterPopup(popup))
            continue;
        localCompleterPopups.append(QPointer<QWidget>(popup));
        popup->installEventFilter(this);
    }
}

void TemporaryEditorDrawer::updateInteractionState()
{
    pruneProtectedPopups();
    const bool active = computedInteractionActive();
    if (active != cachedInteractionActive) {
        cachedInteractionActive = active;
        emit interactionActiveChanged(active);
    }
    if (active)
        cancelAutoCollapse();
    else
        scheduleAutoCollapse();
}

void TemporaryEditorDrawer::updateFocusWithin()
{
    QWidget* focus = QApplication::focusWidget();
    const bool nextFocusWithin = focus
        && (focus == this || isAncestorOf(focus));
    if (focusWithinValue == nextFocusWithin) {
        updateInteractionState();
        return;
    }
    focusWithinValue = nextFocusWithin;
    updateInteractionState();
}

void TemporaryEditorDrawer::scheduleAutoCollapse()
{
    if (!autoCollapseTimer
        || stateValue != State::EdgeStowed
        || !expandedFromHandleValue
        || pinnedValue
        || computedInteractionActive()) {
        return;
    }
    autoCollapseTimer->start(autoCollapseDelayValue);
}

void TemporaryEditorDrawer::cancelAutoCollapse()
{
    if (autoCollapseTimer)
        autoCollapseTimer->stop();
}

void TemporaryEditorDrawer::synchronizeGeometryWithRegion()
{
    if (!parentWidget() || stateValue == State::Hidden)
        return;
    if (stateValue == State::Floating) {
        const QRect next = boundedFloatingGeometry(geometry());
        if (next != geometry())
            setGeometry(next);
        floatingGeometryValue = next;
    } else if (expandedFromHandleValue) {
        setGeometry(expandedGeometryForEdge(edgeValue));
    } else {
        setGeometry(collapsedHandleGeometry(edgeValue));
    }
    updateGripGeometry();
    if (previewEdgeValue != Edge::None)
        showDockPreview(previewEdgeValue);
}

void TemporaryEditorDrawer::setFloatingPreservingGeometry()
{
    if (stateValue == State::Floating)
        return;
    const QRect preserved = geometry();
    stateValue = State::Floating;
    if (edgeValue != Edge::None) {
        edgeValue = Edge::None;
        emit edgeChanged(edgeValue);
    }
    if (expandedFromHandleValue) {
        expandedFromHandleValue = false;
        emit expandedFromHandleChanged(false);
    }
    updateVisibleParts();
    setGeometry(boundedFloatingGeometry(preserved));
    floatingGeometryValue = geometry();
    emit stateChanged(stateValue);
}

void TemporaryEditorDrawer::finishTitleDrag(
    const QPoint& globalPosition)
{
    if (!draggingTitleBar)
        return;
    draggingTitleBar = false;
    titleBar->releaseMouse();
    clearDockPreview();
    const QPoint regionPosition = parentWidget()
        ? parentWidget()->mapFromGlobal(globalPosition)
        : QPoint();
    const Edge destination = edgeAt(regionPosition);
    if (destination != Edge::None) {
        stow(destination);
    } else {
        floatingGeometryValue = geometry();
        preferredSizeValue = geometry().size();
        emit floatingGeometryChanged(floatingGeometryValue);
        emit geometryPreferenceChanged(
            preferredSizeValue, edgeValue);
    }
    updateInteractionState();
}

void TemporaryEditorDrawer::finishGripResize()
{
    if (!resizingWithGrip)
        return;
    resizingWithGrip = false;
    sizeGrip->releaseMouse();
    const QSize previousSize = preferredSizeValue;
    preferredSizeValue = geometry().size();
    if (stateValue == State::Floating) {
        floatingGeometryValue = geometry();
        emit floatingGeometryChanged(floatingGeometryValue);
    }
    if (preferredSizeValue != previousSize)
        emit preferredSizeChanged(preferredSizeValue);
    emit geometryPreferenceChanged(
        preferredSizeValue, edgeValue);
    updateInteractionState();
}

bool TemporaryEditorDrawer::computedInteractionActive() const
{
    if (hoverWithinValue
        || focusWithinValue
        || mousePressedValue
        || draggingTitleBar
        || resizingWithGrip
        || externalInteractionValue) {
        return true;
    }
    for (const QPointer<QWidget>& popup : protectedPopups) {
        if (popup && popup->isVisible())
            return true;
    }
    return false;
}

bool TemporaryEditorDrawer::objectBelongsToDrawer(
    const QObject* object) const
{
    for (const QObject* cursor = object;
         cursor;
         cursor = cursor->parent()) {
        if (cursor == this)
            return true;
    }
    return false;
}

bool TemporaryEditorDrawer::isProtectedPopup(
    const QWidget* widget) const
{
    for (const QPointer<QWidget>& popup : protectedPopups) {
        if (popup == widget)
            return true;
    }
    return false;
}

bool TemporaryEditorDrawer::isLocalCompleterPopup(
    const QWidget* widget) const
{
    for (const QPointer<QWidget>& popup : localCompleterPopups) {
        if (popup == widget)
            return true;
    }
    return false;
}

bool TemporaryEditorDrawer::isPopupLike(
    const QWidget* widget) const
{
    if (!widget || !widget->isWindow())
        return false;
    const Qt::WindowType type = static_cast<Qt::WindowType>(
        static_cast<int>(
            widget->windowFlags() & Qt::WindowType_Mask));
    return type == Qt::Popup
        || type == Qt::ToolTip;
}

bool TemporaryEditorDrawer::isTitleDragSurface(
    const QObject* object) const
{
    return object == titleBar || object == titleLabel;
}

bool TemporaryEditorDrawer::handleTitleBarEvent(QEvent* event)
{
    auto* mouseEvent = dynamic_cast<QMouseEvent*>(event);
    if (!mouseEvent)
        return false;

    if (event->type() == QEvent::MouseButtonPress
        && mouseEvent->button() == Qt::LeftButton) {
        if (stateValue == State::EdgeStowed)
            setFloatingPreservingGeometry();
        draggingTitleBar = true;
        mousePressedValue = true;
        const QPoint globalTopLeft = parentWidget()
            ? parentWidget()->mapToGlobal(geometry().topLeft())
            : geometry().topLeft();
        dragOffset = mouseEvent->globalPosition().toPoint()
            - globalTopLeft;
        titleBar->grabMouse();
        cancelAutoCollapse();
        updateInteractionState();
        return true;
    }

    if (event->type() == QEvent::MouseMove
        && draggingTitleBar
        && parentWidget()) {
        const QPoint globalTopLeft =
            mouseEvent->globalPosition().toPoint() - dragOffset;
        const QPoint localTopLeft =
            parentWidget()->mapFromGlobal(globalTopLeft);
        setGeometry(boundedFloatingGeometry(
            QRect(localTopLeft, size())));
        const QPoint regionPosition = parentWidget()->mapFromGlobal(
            mouseEvent->globalPosition().toPoint());
        const Edge destination = edgeAt(regionPosition);
        if (destination == Edge::None)
            clearDockPreview();
        else
            showDockPreview(destination);
        raise();
        return true;
    }

    if (event->type() == QEvent::MouseButtonRelease
        && mouseEvent->button() == Qt::LeftButton
        && draggingTitleBar) {
        mousePressedValue = false;
        finishTitleDrag(mouseEvent->globalPosition().toPoint());
        return true;
    }
    return false;
}

bool TemporaryEditorDrawer::handleSizeGripEvent(QEvent* event)
{
    auto* mouseEvent = dynamic_cast<QMouseEvent*>(event);
    if (!mouseEvent)
        return false;

    if (event->type() == QEvent::MouseButtonPress
        && mouseEvent->button() == Qt::LeftButton) {
        resizingWithGrip = true;
        mousePressedValue = true;
        resizeStartGlobal = mouseEvent->globalPosition().toPoint();
        resizeStartGeometry = geometry();
        sizeGrip->grabMouse();
        cancelAutoCollapse();
        updateInteractionState();
        return true;
    }

    if (event->type() == QEvent::MouseMove
        && resizingWithGrip
        && parentWidget()) {
        const QPoint delta = mouseEvent->globalPosition().toPoint()
            - resizeStartGlobal;
        const QSize requested(
            resizeStartGeometry.width() + delta.x(),
            resizeStartGeometry.height() + delta.y());
        const QSize bounded = boundedPreferredSize(requested);
        QRect next = resizeStartGeometry;
        next.setSize(bounded);
        if (stateValue == State::EdgeStowed) {
            if (edgeValue == Edge::Right)
                next.moveRight(parentWidget()->rect().right());
            if (edgeValue == Edge::Bottom)
                next.moveBottom(parentWidget()->rect().bottom());
        }
        next = boundedFloatingGeometry(next);
        setGeometry(next);
        return true;
    }

    if (event->type() == QEvent::MouseButtonRelease
        && mouseEvent->button() == Qt::LeftButton
        && resizingWithGrip) {
        mousePressedValue = false;
        finishGripResize();
        return true;
    }
    return false;
}

void TemporaryEditorDrawer::trackPopup(
    QWidget* popup,
    bool visible)
{
    if (!popup)
        return;
    if (visible) {
        if (!isProtectedPopup(popup)) {
            protectedPopups.append(QPointer<QWidget>(popup));
        }
        if (!popupDestroyedConnections.contains(popup)) {
            const QMetaObject::Connection connection =
                connect(popup,
                        &QObject::destroyed,
                        this,
                        [this, popup]() {
                            popupDestroyedConnections.remove(popup);
                            QTimer::singleShot(0, this, [this]() {
                                pruneProtectedPopups();
                                updateInteractionState();
                                updateApplicationEventFilterNeed();
                            });
                        });
            popupDestroyedConnections.insert(popup, connection);
        }
    } else {
        for (qsizetype index = protectedPopups.size() - 1;
             index >= 0;
             --index) {
            if (!protectedPopups.at(index)
                || protectedPopups.at(index) == popup) {
                protectedPopups.removeAt(index);
            }
        }
    }
    updateInteractionState();
}

void TemporaryEditorDrawer::pruneProtectedPopups()
{
    for (qsizetype index = protectedPopups.size() - 1;
         index >= 0;
         --index) {
        if (!protectedPopups.at(index))
            protectedPopups.removeAt(index);
    }
    for (qsizetype index = localCompleterPopups.size() - 1;
         index >= 0;
         --index) {
        if (!localCompleterPopups.at(index))
            localCompleterPopups.removeAt(index);
    }
}

TemporaryEditorDrawer::Edge TemporaryEditorDrawer::edgeAt(
    const QPoint& editorRegionPosition) const
{
    if (!parentWidget())
        return Edge::None;
    return edgeForSplitDirection(
        EditorDropPreviewOverlay::directionAt(
            parentWidget()->rect(), editorRegionPosition));
}

QRect TemporaryEditorDrawer::boundedFloatingGeometry(
    const QRect& requestedGeometry) const
{
    if (!parentWidget())
        return requestedGeometry;
    const QRect region = parentWidget()->rect();
    if (!region.isValid())
        return {};

    QSize requestedSize = requestedGeometry.isValid()
        ? requestedGeometry.size()
        : preferredSizeValue;
    requestedSize = boundedPreferredSize(requestedSize);
    QPoint requestedTopLeft = requestedGeometry.isValid()
        ? requestedGeometry.topLeft()
        : QPoint(
              region.center().x() - requestedSize.width() / 2,
              region.center().y() - requestedSize.height() / 2);
    const int maximumX = region.right() - requestedSize.width() + 1;
    const int maximumY = region.bottom() - requestedSize.height() + 1;
    requestedTopLeft.setX(qBound(
        region.left(), requestedTopLeft.x(), qMax(region.left(), maximumX)));
    requestedTopLeft.setY(qBound(
        region.top(), requestedTopLeft.y(), qMax(region.top(), maximumY)));
    return QRect(requestedTopLeft, requestedSize);
}

QRect TemporaryEditorDrawer::expandedGeometryForEdge(
    Edge requestedEdge) const
{
    if (!parentWidget() || requestedEdge == Edge::None)
        return boundedFloatingGeometry(floatingGeometryValue);
    const QRect region = parentWidget()->rect();
    const QSize expandedSize = boundedPreferredSize(preferredSizeValue);
    QRect base;
    if (floatingGeometryValue.isValid()) {
        base = floatingGeometryValue;
        base.setSize(expandedSize);
        base = boundedFloatingGeometry(base);
    } else {
        base = boundedFloatingGeometry(QRect());
    }
    switch (requestedEdge) {
    case Edge::Left:
        base.moveLeft(region.left());
        break;
    case Edge::Right:
        base.moveRight(region.right());
        break;
    case Edge::Top:
        base.moveTop(region.top());
        break;
    case Edge::Bottom:
        base.moveBottom(region.bottom());
        break;
    case Edge::None:
        break;
    }
    return base;
}

QRect TemporaryEditorDrawer::collapsedHandleGeometry(
    Edge requestedEdge) const
{
    if (!parentWidget() || requestedEdge == Edge::None)
        return {};
    const QRect region = parentWidget()->rect();
    if (!region.isValid())
        return {};
    const bool vertical = isVerticalEdge(requestedEdge);
    const int availableLength = vertical
        ? region.height()
        : region.width();
    const int handleLength = qMax(
        1, qMin(kMaximumHandleLength, availableLength));
    const QSize handleSize = vertical
        ? QSize(qMin(kHandleThickness, region.width()), handleLength)
        : QSize(handleLength, qMin(kHandleThickness, region.height()));

    QRect expanded = expandedGeometryForEdge(requestedEdge);
    QPoint topLeft;
    switch (requestedEdge) {
    case Edge::Left:
        topLeft = QPoint(
            region.left(),
            qBound(
                region.top(),
                expanded.center().y() - handleSize.height() / 2,
                region.bottom() - handleSize.height() + 1));
        break;
    case Edge::Right:
        topLeft = QPoint(
            region.right() - handleSize.width() + 1,
            qBound(
                region.top(),
                expanded.center().y() - handleSize.height() / 2,
                region.bottom() - handleSize.height() + 1));
        break;
    case Edge::Top:
        topLeft = QPoint(
            qBound(
                region.left(),
                expanded.center().x() - handleSize.width() / 2,
                region.right() - handleSize.width() + 1),
            region.top());
        break;
    case Edge::Bottom:
        topLeft = QPoint(
            qBound(
                region.left(),
                expanded.center().x() - handleSize.width() / 2,
                region.right() - handleSize.width() + 1),
            region.bottom() - handleSize.height() + 1);
        break;
    case Edge::None:
        return {};
    }
    return QRect(topLeft, handleSize);
}

QSize TemporaryEditorDrawer::boundedPreferredSize(
    const QSize& requestedSize) const
{
    QSize requested = requestedSize.isValid()
        ? requestedSize
        : QSize(640, 440);
    if (!parentWidget()) {
        return QSize(
            qMax(kMinimumExpandedWidth, requested.width()),
            qMax(kMinimumExpandedHeight, requested.height()));
    }
    const QSize region = parentWidget()->size();
    const int maximumWidth = qMax(1, region.width());
    const int maximumHeight = qMax(1, region.height());
    const int minimumWidth = qMin(
        kMinimumExpandedWidth, maximumWidth);
    const int minimumHeight = qMin(
        kMinimumExpandedHeight, maximumHeight);
    return QSize(
        qBound(minimumWidth, requested.width(), maximumWidth),
        qBound(minimumHeight, requested.height(), maximumHeight));
}
