#include "uiwindowchrome.h"
#include "applicationthememanager.h"
#include "roundedicons.h"
#include "uitypography.h"
#include <QAbstractButton>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QToolButton>

#ifdef ZEROSLACK_ENABLE_ELA
#include "ElaAppBar.h"
#include "ElaIconButton.h"
#include "ElaTheme.h"
#include "ElaToolButton.h"
#endif

UiWindowTitleBar UiWindowChrome::createTitleBar(QMainWindow* host)
{
    UiWindowTitleBar result;
    QAbstractButton* minimize = nullptr;
    QAbstractButton* close = nullptr;
#ifdef ZEROSLACK_ENABLE_ELA
    if (ApplicationThemeManager::instance().backend() == UiStyleBackend::Ela) {
        auto* bar = new ElaAppBar(host, ElaAppBar::WindowManagement::External);
        bar->setWindowButtonFlags(ElaAppBarType::NavigationButtonHint
            | ElaAppBarType::MinimizeButtonHint | ElaAppBarType::MaximizeButtonHint
            | ElaAppBarType::CloseButtonHint);
        result.widget = bar;
        result.label = bar->titleLabel();
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
    }
#endif
    if (!result.widget) {
        auto* frame = new QFrame(host);
        result.widget = frame;
        frame->setFixedHeight(36);
        auto* row = new QHBoxLayout(frame);
        row->setContentsMargins(8, 0, 2, 0);
        result.sidebar = new QToolButton(frame);
        row->addWidget(result.sidebar);
        result.label = new QLabel(host->windowTitle(), frame);
        row->addWidget(result.label, 1);
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
        QObject::connect(host, &QWidget::windowTitleChanged, result.label, &QLabel::setText);
    }
    result.widget->setObjectName(QStringLiteral("workspaceTitleBar"));
    result.widget->setAutoFillBackground(true);
    result.label->setObjectName(QStringLiteral("workspaceFilePath"));
    result.label->setTextFormat(Qt::PlainText);
    result.label->setWordWrap(false);
    result.label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    result.label->setMinimumWidth(0);
    UiTypography::apply(result.label, UiTypography::Role::Body);
    result.sidebar->setObjectName(QStringLiteral("expandProjectSidebarButton"));
    result.sidebar->setIcon(RoundedIcons::icon(RoundedIcons::Sidebar));
    result.sidebar->setIconSize(QSize(20, 20));
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
