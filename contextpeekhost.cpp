#include "contextpeekhost.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QShortcut>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {
constexpr int kMinimumPeekWidth = 280;
constexpr int kMaximumPeekWidth = 920;
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

int ContextPeekHost::preferredWidth() const
{
    return preferredWidthValue;
}

void ContextPeekHost::setPreferredWidth(int width)
{
    const int bounded = std::clamp(
        width, kMinimumPeekWidth, kMaximumPeekWidth);
    if (preferredWidthValue == bounded)
        return;
    preferredWidthValue = bounded;
    synchronizeGeometry();
}

void ContextPeekHost::setActionsAvailable(
    bool pinAvailable,
    bool fullViewAvailable)
{
    pinButton->setVisible(pinAvailable);
    pinButton->setEnabled(pinAvailable);
    fullViewButton->setVisible(fullViewAvailable);
    fullViewButton->setEnabled(fullViewAvailable);
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
    if (watched == parentWidget() && event) {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::LayoutRequest:
            synchronizeGeometry();
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ContextPeekHost::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    header = new QWidget(this);
    header->setObjectName(QStringLiteral("contextPeekHeader"));
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(8, 4, 4, 4);
    headerLayout->setSpacing(4);
    titleLabel = new QLabel(header);
    titleLabel->setObjectName(QStringLiteral("contextPeekTitle"));
    titleLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    headerLayout->addWidget(titleLabel, 1);

    pinButton = new QToolButton(header);
    pinButton->setObjectName(QStringLiteral("contextPeekPin"));
    pinButton->setIcon(
        style()->standardIcon(QStyle::SP_TitleBarNormalButton));
    pinButton->setToolTip(tr("Pin to context workspace"));
    headerLayout->addWidget(pinButton);

    fullViewButton = new QToolButton(header);
    fullViewButton->setObjectName(QStringLiteral("contextPeekFullView"));
    fullViewButton->setIcon(
        style()->standardIcon(QStyle::SP_TitleBarMaxButton));
    fullViewButton->setToolTip(tr("Open full view"));
    headerLayout->addWidget(fullViewButton);

    closeButton = new QToolButton(header);
    closeButton->setObjectName(QStringLiteral("contextPeekClose"));
    closeButton->setIcon(
        style()->standardIcon(QStyle::SP_DialogCloseButton));
    closeButton->setToolTip(tr("Close preview"));
    headerLayout->addWidget(closeButton);
    root->addWidget(header);

    contentHost = new QWidget(this);
    contentHost->setObjectName(QStringLiteral("contextPeekContent"));
    contentLayout = new QVBoxLayout(contentHost);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    root->addWidget(contentHost, 1);

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
            &ContextPeekHost::closeRequested);
}

void ContextPeekHost::synchronizeGeometry()
{
    QWidget* region = parentWidget();
    if (!region)
        return;
    const QRect available = region->contentsRect();
    if (available.isEmpty())
        return;
    const int width = std::clamp(
        preferredWidthValue,
        qMin(kMinimumPeekWidth, available.width()),
        available.width());
    setGeometry(available.right() - width + 1,
                available.top(),
                width,
                available.height());
}
