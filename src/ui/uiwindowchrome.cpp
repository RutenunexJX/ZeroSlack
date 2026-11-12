#include "uiwindowchrome.h"
#include "applicationthememanager.h"
#include "roundedicons.h"
#include "uitypography.h"
#include "version.h"
#include <QAbstractButton>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QToolButton>
#include <QPainter>
#include <QTimer>

namespace {
class WindowBoundary final : public QWidget {
public:
    explicit WindowBoundary(QWidget* host) : QWidget(host) {
        setObjectName(QStringLiteral("windowBoundary"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
        host->installEventFilter(this);
        connect(&ApplicationThemeManager::instance(),&ApplicationThemeManager::themeChanged,this,[this]{update();});
        refresh();
    }
protected:
    bool eventFilter(QObject*,QEvent* event) override {
        if (event->type()==QEvent::Resize || event->type()==QEvent::Show
            || event->type()==QEvent::WindowStateChange) refresh();
        if (event->type()==QEvent::ChildAdded)
            QTimer::singleShot(0,this,[this]{raise();});
        return false;
    }
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        const bool dark=isDarkTheme(ApplicationThemeManager::instance().mode());
        const qreal width=parentWidget()->property("settingsWindowChrome").toBool() ? 2 : 1;
        painter.setPen(QPen(dark ? QColor(112,118,130) : QColor(137,143,154),width));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(rect()).adjusted(width/2,width/2,-width/2,-width/2));
    }
private:
    void refresh() {
        setGeometry(parentWidget()->rect());
        setVisible(parentWidget()->isWindow() && !parentWidget()->isMaximized() && !parentWidget()->isFullScreen());
        raise();
    }
};
}

void UiWindowChrome::ensureWindowBoundary(QWidget* window)
{
    if (window && !window->findChild<QWidget*>(QStringLiteral("windowBoundary"),Qt::FindDirectChildrenOnly))
        new WindowBoundary(window);
}

#ifdef ZEROSLACK_ENABLE_ELA
#include "ElaAppBar.h"
#include "ElaIconButton.h"
#include "ElaTheme.h"
#include "ElaToolButton.h"
#endif

UiWindowTitleBar UiWindowChrome::createTitleBar(QMainWindow* host, QWidget* workspacePicker,
                                             QWidget* accessory)
{
    UiWindowTitleBar result;
    QAbstractButton* minimize = nullptr;
    QAbstractButton* close = nullptr;
#ifdef ZEROSLACK_ENABLE_ELA
    auto* bar = new ElaAppBar(host, ElaAppBar::WindowManagement::External);
    bar->setWindowButtonFlags(ElaAppBarType::NavigationButtonHint
        | ElaAppBarType::MinimizeButtonHint | ElaAppBarType::MaximizeButtonHint
        | ElaAppBarType::CloseButtonHint);
    result.widget = bar;
    if (workspacePicker)
        bar->setCustomWidget(ElaAppBarType::LeftArea, workspacePicker);
    if (accessory)
        bar->setCustomWidget(ElaAppBarType::RightArea, accessory);
    result.label = bar->titleLabel();
    if (workspacePicker) {
        QObject::disconnect(host, &QWidget::windowTitleChanged, bar, nullptr);
        if (auto* row = qobject_cast<QHBoxLayout*>(bar->layout())) {
            row->setStretch(0, 0);
            row->itemAt(0)->setAlignment(Qt::AlignLeft);
        }
    }
    result.sidebar = qobject_cast<QToolButton*>(bar->windowButton(ElaAppBarType::NavigationButtonHint));
    result.maximize = qobject_cast<QToolButton*>(bar->windowButton(ElaAppBarType::MaximizeButtonHint));
    minimize = bar->windowButton(ElaAppBarType::MinimizeButtonHint);
    close = bar->windowButton(ElaAppBarType::CloseButtonHint);
    result.sidebar->setProperty("ElaIconType", QVariant());
    const auto refreshWindowIcons = [bar] {
        bar->setWindowButtonIcons(RoundedIcons::icon(RoundedIcons::Minimize),
            RoundedIcons::icon(RoundedIcons::Maximize), RoundedIcons::icon(RoundedIcons::Restore));
    };
    QObject::connect(eTheme, &ElaTheme::themeModeChanged, bar, refreshWindowIcons);
    refreshWindowIcons();
    qobject_cast<QToolButton*>(minimize)->setIconSize(QSize(16, 16));
    result.maximize->setIconSize(QSize(16, 16));
    close->setIcon(RoundedIcons::icon(RoundedIcons::Close));
    close->setIconSize(QSize(16, 16));
    int buttonHeight = 30;
    for (auto* button : {static_cast<QAbstractButton*>(result.sidebar),
                        static_cast<QAbstractButton*>(result.maximize), minimize, close}) {
        button->setProperty("zeroslackElaControl", true);
        button->setFocusPolicy(Qt::StrongFocus);
        buttonHeight = qMax(buttonHeight, button->minimumSizeHint().height());
    }
    for (auto* button : {static_cast<QAbstractButton*>(result.sidebar),
                        static_cast<QAbstractButton*>(result.maximize), minimize, close})
        button->setFixedSize(qMax(40, button->minimumSizeHint().width()), buttonHeight);
    bar->setAppBarHeight(qMax(36, buttonHeight + 4));
    bar->setProperty("zeroslackElaControl", true);
    auto* closeIcon = qobject_cast<ElaIconButton*>(close);
    closeIcon->setBorderRadius(4);
    closeIcon->setAutoDefault(false);
    const auto refreshClose = [closeIcon] {
        closeIcon->setLightIconColor(ElaThemeColor(ElaThemeType::Light, BasicText));
        closeIcon->setDarkIconColor(ElaThemeColor(ElaThemeType::Dark, BasicText));
        closeIcon->update();
    };
    QObject::connect(eTheme, &ElaTheme::themeModeChanged, closeIcon, refreshClose);
    refreshClose();
#else
    auto* frame = new QFrame(host);
    result.widget = frame;
    frame->setFixedHeight(36);
    auto* row = new QHBoxLayout(frame);
    row->setContentsMargins(8, 0, 2, 0);
    result.sidebar = new QToolButton(frame);
    row->addWidget(result.sidebar);
    result.label = new QLabel(host->windowTitle(), frame);
    row->addWidget(result.label, workspacePicker ? 0 : 1);
    if (workspacePicker) {
        row->addWidget(workspacePicker);
        row->addStretch();
    }
    if (accessory)
        row->addWidget(accessory);
    for (int index = 0; index < 3; ++index) {
        auto* button = new QToolButton(frame);
        button->setIcon(host->style()->standardIcon(index == 0 ? QStyle::SP_TitleBarMinButton
            : index == 1 ? QStyle::SP_TitleBarMaxButton : QStyle::SP_TitleBarCloseButton));
        row->addWidget(button);
        if (index == 0) minimize = button;
        else if (index == 1) result.maximize = button;
        else {
            close = button;
            button->setStyleSheet(QStringLiteral("QToolButton:hover { background:#c42b1c; color:white; }"));
        }
        QObject::connect(button, &QToolButton::clicked, host, [host, index] {
            if (index == 0) host->showMinimized();
            else if (index == 1) host->isMaximized() ? host->showNormal() : host->showMaximized();
            else host->close();
        });
    }
    if (!workspacePicker)
        QObject::connect(host, &QWidget::windowTitleChanged, result.label, &QLabel::setText);
#endif
    result.widget->setObjectName(QStringLiteral("workspaceTitleBar"));
    result.widget->setAutoFillBackground(true);
    result.label->setObjectName(QStringLiteral("workspaceFilePath"));
    result.label->setTextFormat(Qt::PlainText);
    result.label->setWordWrap(false);
    result.label->setSizePolicy(workspacePicker ? QSizePolicy::Minimum : QSizePolicy::Ignored,
                               QSizePolicy::Preferred);
    result.label->setMinimumWidth(0);
    UiTypography::apply(result.label, UiTypography::Role::Body);
    if (workspacePicker) {
        // Document window titles may contain paths; the visible app identity stays fixed.
        result.label->setText(QStringLiteral("ZeroSlack v%1").arg(QLatin1String(APP_VERSION)));
        result.label->show();
    }
    result.sidebar->setObjectName(QStringLiteral("expandProjectSidebarButton"));
    result.sidebar->setIcon(RoundedIcons::icon(RoundedIcons::Sidebar));
    result.sidebar->setIconSize(QSize(RoundedIcons::SidebarSize, RoundedIcons::SidebarSize));
    result.sidebar->setToolTip(QObject::tr("Expand sidebar (Ctrl+1)"));
    result.sidebar->setAccessibleName(QObject::tr("Expand sidebar"));
    minimize->setObjectName(QStringLiteral("windowMinimizeButton"));
    minimize->setToolTip(QObject::tr("Minimize"));
    minimize->setAccessibleName(minimize->toolTip());
    result.maximize->setObjectName(QStringLiteral("windowMaximizeButton"));
    result.maximize->setToolTip(QObject::tr("Maximize / Restore"));
    result.maximize->setAccessibleName(result.maximize->toolTip());
    close->setObjectName(QStringLiteral("windowCloseButton"));
    close->setToolTip(QObject::tr("Close"));
    close->setAccessibleName(close->toolTip());
    return result;
}
