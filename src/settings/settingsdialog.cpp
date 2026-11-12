#include "settingsdialog.h"
#include "settingscenterpanel.h"
#include "uicontrols.h"
#include "applicationthememanager.h"
#include "insightvisualstyle.h"
#ifdef ZEROSLACK_ENABLE_ELA
#include "ElaAppBar.h"
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

#include <QApplication>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QListWidget>
#include <QScopedValueRollback>
#include <QSet>
#include <QScrollArea>
#include <QSettings>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

namespace {
constexpr auto geometryKey = "ui/settingsDialog/geometry";
constexpr auto screenNameKey = "ui/settingsDialog/screenName";
constexpr auto screenSerialKey = "ui/settingsDialog/screenSerialNumber";

QSettings uiSettings()
{
    return QSettings(QSettings::defaultFormat(), QSettings::UserScope,
                     QStringLiteral("ZeroSlack"), QStringLiteral("ZeroSlack"));
}

QMargins frameMargins(const QWidget* widget)
{
    const QRect frame = widget->frameGeometry(), client = widget->geometry();
    return {qMax(0, client.left() - frame.left()), qMax(0, client.top() - frame.top()),
            qMax(0, frame.right() - client.right()), qMax(0, frame.bottom() - client.bottom())};
}
}

int SettingsDialogGeometry::selectScreen(const QList<Screen>& screens, const QString& name,
                                         const QString& serial, int primary)
{
    int matched = -1;
    for (int i = 0; i < screens.size(); ++i) {
        if (!name.isEmpty() && screens[i].name == name
            && (serial.isEmpty() || screens[i].serial == serial)) {
            if (matched >= 0)
                return primary >= 0 && primary < screens.size() ? primary : (screens.isEmpty() ? -1 : 0);
            matched = i;
        }
    }
    return matched >= 0 ? matched
        : (primary >= 0 && primary < screens.size() ? primary : (screens.isEmpty() ? -1 : 0));
}

QRect SettingsDialogGeometry::constrain(const QRect& client, const QRect& available,
                                        const QMargins& frame, const QSize& preferred)
{
    if (!available.isValid())
        return client;
    const QSize wanted = client.isValid() ? client.size() : preferred;
    const QSize size(qBound(1, wanted.width(), qMax(1, available.width() - frame.left() - frame.right())),
                     qBound(1, wanted.height(), qMax(1, available.height() - frame.top() - frame.bottom())));
    QRect outer(QPoint(), size + QSize(frame.left() + frame.right(), frame.top() + frame.bottom()));
    outer.moveTopLeft(client.isValid() ? client.topLeft() - QPoint(frame.left(), frame.top())
                                      : available.center() - QPoint(outer.width() / 2, outer.height() / 2));
    outer.moveLeft(qBound(available.left(), outer.left(), available.right() - outer.width() + 1));
    outer.moveTop(qBound(available.top(), outer.top(), available.bottom() - outer.height() + 1));
    return QRect(outer.topLeft() + QPoint(frame.left(), frame.top()), size);
}

SettingsDialog::SettingsDialog(SettingsCenterPanel* panel, QWidget* parent)
    : UiDialog(parent), settingsPanel(panel)
{
    setObjectName(QStringLiteral("settingsDialog"));
    setProperty("settingsWindowChrome", true);
    setWindowTitle(tr("Settings"));
    setWindowModality(Qt::NonModal);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setWindowFlag(Qt::WindowStaysOnTopHint, false);
    setWindowFlag(Qt::WindowMinimizeButtonHint, false);
    setWindowFlag(Qt::WindowMaximizeButtonHint, false);
    auto* body = new QWidget(this);
    body->setObjectName(QStringLiteral("settingsDialogBody"));
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(0, 0, 0, 12);
    layout->setSizeConstraint(QLayout::SetNoConstraint);
    auto* scroll = UiControls::scrollArea(body);
    scroll->setObjectName(QStringLiteral("settingsDialogScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setMinimumSize(0, 0);
    panel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    scroll->setWidget(panel);
    layout->addWidget(scroll, 1);
    auto* buttons = new QHBoxLayout;
    buttons->setContentsMargins(16, 0, 16, 0);
    buttons->addStretch();
    auto* closeButton = UiControls::pushButton(tr("Close"), body);
    closeButton->setObjectName(QStringLiteral("settingsDialogCloseButton"));
    closeButton->setAutoDefault(false);
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);
    // Ela's title-bar buttons also participate in QDialog default-button lookup.
    for (auto* button : findChildren<QPushButton*>()) {
        button->setAutoDefault(false);
        button->setDefault(false);
    }
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(body);
    this->layout()->setSizeConstraint(QLayout::SetNoConstraint);
    setMinimumSize(0, 0);
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
    for (auto* screen : QGuiApplication::screens())
        watchScreen(screen);
    connect(qApp, &QGuiApplication::screenAdded, this, [this](QScreen* screen) {
        watchScreen(screen);
        if (isVisible()) constrainPlacement();
    });
    connect(qApp, &QGuiApplication::screenRemoved, this, [this] {
        if (isVisible()) QTimer::singleShot(0, this, &SettingsDialog::constrainPlacement);
    });
    connect(&ApplicationThemeManager::instance(), &ApplicationThemeManager::themeChanged,
            this, &SettingsDialog::refreshSurfaces);
    refreshSurfaces();
}

void SettingsDialog::refreshSurfaces()
{
    const auto& theme = InsightVisualStyle::theme();
    setStyleSheet(QStringLiteral(
        "QWidget#settingsDialogBody { background: %1; }"
        "QWidget#settingsCenterNavigationSurface { background: %2; border-radius: 8px; }"
        "QWidget#settingsCenterContentSurface { background: %3; border-radius: 8px; }"
        "QListWidget#settingsCenterCategoryList { background: transparent; border: 0; }"
        "QScrollArea#settingsDialogScroll { background: %1; border: 0; }")
        .arg(theme.surface.canvas.name(), theme.panelSubtle.name(), theme.surface.panel.name()));
#ifdef ZEROSLACK_ENABLE_ELA
    if (auto* bar = findChild<ElaAppBar*>(QString(), Qt::FindDirectChildrenOnly)) {
        auto colors = bar->palette();
        colors.setColor(QPalette::Window, theme.surface.raised);
        bar->setPalette(colors);
        bar->setStyleSheet(QStringLiteral("ElaAppBar { background: %1; border-bottom: 1px solid %2; }")
            .arg(theme.surface.raised.name(), theme.border.name()));
        bar->setAttribute(Qt::WA_StyledBackground);
    }
#endif
}

void SettingsDialog::showSettings()
{
    if (confirming || hostClosePrepared)
        return;
    QPointer<QWidget> editor = focusWidget();
    if (!editor || !settingsPanel->isAncestorOf(editor))
        editor = lastEditor;
    if (!placementLoaded)
        restorePlacement();
    show();
    raise();
    activateWindow();
    if (editor && editor->isVisible() && editor->isEnabled())
        editor->setFocus(Qt::OtherFocusReason);
    else
        settingsPanel->setFocus(Qt::OtherFocusReason);
}

bool SettingsDialog::confirmDiscard()
{
    if (confirming || hostClosePrepared)
        return false;
    lastEditor = QApplication::focusWidget();
    if (lastEditor && !settingsPanel->isAncestorOf(lastEditor))
        lastEditor.clear();
    settingsPanel->commitPendingEditors();
    auto scopes = settingsPanel->pendingScopes();
    if (scopes.isEmpty())
        return true;
    const QScopedValueRollback<bool> guard(confirming, true);
    QSet<QString> selected;
    for (const auto& scope : scopes)
        if (scope.current) selected.insert(scope.key);
    QStringList outcomes;
    while (!scopes.isEmpty()) {
        UiMessageDialog message(this);
        message.setObjectName(QStringLiteral("settingsDiscardConfirmation"));
        message.setWindowTitle(tr("Unapplied settings"));
        const bool multiple = scopes.size() > 1;
        message.setText(multiple ? tr("Choose the changes to apply. Unselected drafts remain open.")
            : tr("Apply changes to %1 before closing?").arg(scopes.first().label));
        if (!outcomes.isEmpty()) message.setText(outcomes.join(QLatin1Char('\n'))
            + QStringLiteral("\n\n") + message.text());
        QListWidget* list = nullptr;
        if (multiple) {
            list = UiControls::listWidget(&message);
            list->setObjectName(QStringLiteral("settingsPendingScopes"));
            list->setWordWrap(true);
            list->setSelectionMode(QAbstractItemView::NoSelection);
            for (const auto& scope : scopes) {
                auto* item = new QListWidgetItem(scope.label, list);
                item->setData(Qt::UserRole, scope.key);
                item->setToolTip(scope.label);
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
                item->setCheckState(selected.contains(scope.key) ? Qt::Checked : Qt::Unchecked);
            }
            message.setContentWidget(list);
        }
        auto* apply = message.addButton(multiple ? tr("Apply Selected") : tr("Apply and Close"), QMessageBox::AcceptRole);
        auto* discard = message.addButton(tr("Discard Changes"), QMessageBox::DestructiveRole);
        auto* keep = message.addButton(tr("Keep Editing"), QMessageBox::RejectRole);
        apply->setObjectName(QStringLiteral("settingsApplyOnClose"));
        keep->setObjectName(QStringLiteral("settingsKeepEditing"));
        discard->setObjectName(QStringLiteral("settingsDiscardUnapplied"));
        if (list) {
            const auto updateSelection = [list, apply, &selected] {
                selected.clear();
                for (int i = 0; i < list->count(); ++i)
                    if (list->item(i)->checkState() == Qt::Checked)
                        selected.insert(list->item(i)->data(Qt::UserRole).toString());
                apply->setEnabled(!selected.isEmpty());
            };
            connect(list, &QListWidget::itemChanged, &message, updateSelection);
            updateSelection();
        } else {
            selected = {scopes.first().key};
        }
        message.setDefaultButton(keep);
        message.setEscapeButton(keep);
        message.exec();
        if (message.clickedButton() == discard) return true;
        if (message.clickedButton() != apply) {
            if (lastEditor) lastEditor->setFocus(Qt::OtherFocusReason);
            return false;
        }
        outcomes.clear();
        for (const auto& scope : scopes) {
            if (!selected.contains(scope.key)) continue;
            const auto result = settingsPanel->applyDraft(scope.key);
            outcomes.append(result.saved ? tr("Applied: %1").arg(scope.label)
                : tr("%1: %2").arg(scope.label, result.message));
        }
        scopes = settingsPanel->pendingScopes();
    }
    return true;
}

void SettingsDialog::closeEvent(QCloseEvent* event)
{
    if (!confirmDiscard()) {
        event->ignore();
        return;
    }
    settingsPanel->discardUnappliedChanges();
    savePlacement();
    event->accept();
}

void SettingsDialog::reject()
{
    close();
}

bool SettingsDialog::prepareForHostClose()
{
    if (!confirmDiscard())
        return false;
    hostClosePrepared = true;
    return true;
}

void SettingsDialog::completeHostClose(bool accepted)
{
    if (!hostClosePrepared)
        return;
    hostClosePrepared = false;
    if (accepted) {
        settingsPanel->discardUnappliedChanges();
        savePlacement();
        hide();
    }
}

void SettingsDialog::restorePlacement()
{
    auto settings = uiSettings();
    const auto screens = QGuiApplication::screens();
    QList<SettingsDialogGeometry::Screen> available;
    for (auto* screen : screens)
        available.append({screen->name(), screen->serialNumber(), screen->availableGeometry()});
    const int selected = SettingsDialogGeometry::selectScreen(available,
        settings.value(screenNameKey).toString(), settings.value(screenSerialKey).toString(),
        screens.indexOf(QGuiApplication::primaryScreen()));
    if (selected < 0)
        return;
    const QSize preferred = settingsPanel->sizeHint().expandedTo(
        QSize(fontMetrics().horizontalAdvance(QLatin1Char('M')) * 82, fontMetrics().lineSpacing() * 32));
    setGeometry(SettingsDialogGeometry::constrain(settings.value(geometryKey).toRect(),
        available[selected].available, frameMargins(this), preferred));
    placementLoaded = true;
}

void SettingsDialog::constrainPlacement()
{
    auto* target = QGuiApplication::screenAt(frameGeometry().center());
    if (!target) target = QGuiApplication::primaryScreen();
    if (!target && !QGuiApplication::screens().isEmpty()) target = QGuiApplication::screens().first();
    if (target)
        setGeometry(SettingsDialogGeometry::constrain(geometry(), target->availableGeometry(),
                                                     frameMargins(this), sizeHint()));
}

void SettingsDialog::showEvent(QShowEvent* event)
{
    UiDialog::showEvent(event);
#ifdef Q_OS_WIN
    // Ela's compatibility shadow path excludes Qt 6.10. Apply DWM only to
    // this top-level surface; its native resize and focus handling stay intact.
    if (QGuiApplication::platformName() == QStringLiteral("windows")) {
        const MARGINS shadow{1, 1, 1, 1};
        DwmExtendFrameIntoClientArea(reinterpret_cast<HWND>(winId()), &shadow);
    }
#endif
    if (windowHandle())
        connect(windowHandle(), &QWindow::screenChanged, this,
                &SettingsDialog::constrainPlacement, Qt::UniqueConnection);
    QTimer::singleShot(0, this, &SettingsDialog::constrainPlacement);
}

bool SettingsDialog::event(QEvent* event)
{
    const bool dpiChanged = event->type() == QEvent::DevicePixelRatioChange;
    const bool handled = UiDialog::event(event);
    if (dpiChanged && isVisible())
        QTimer::singleShot(0, this, &SettingsDialog::constrainPlacement);
    return handled;
}

void SettingsDialog::savePlacement()
{
    if (!isVisible() || !geometry().isValid())
        return;
    constrainPlacement();
    auto* screen = QGuiApplication::screenAt(frameGeometry().center());
    if (!screen || !screen->availableGeometry().contains(frameGeometry()))
        return;
    auto settings = uiSettings();
    settings.setValue(geometryKey, geometry());
    settings.setValue(screenNameKey, screen->name());
    settings.setValue(screenSerialKey, screen->serialNumber());
}

void SettingsDialog::watchScreen(QScreen* screen)
{
    const auto adapt = [this] { if (isVisible()) constrainPlacement(); };
    connect(screen, &QScreen::availableGeometryChanged, this, adapt);
    connect(screen, &QScreen::logicalDotsPerInchChanged, this, adapt);
}
