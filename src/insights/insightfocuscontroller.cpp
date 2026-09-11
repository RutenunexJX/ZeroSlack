#include "insightfocuscontroller.h"

#include <QAction>
#include <QBoxLayout>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>

#include <utility>

InsightFocusController::InsightFocusController(
    QStackedWidget* centralStack,
    QWidget* editorPage,
    QObject* parent)
    : QObject(parent)
    , stack(centralStack)
    , editor(editorPage)
{
    if (!stack || !editor)
        return;

    page = new QWidget(stack);
    page->setObjectName(QStringLiteral("insightFocusPage"));
    page->setMinimumSize(640, 360);
    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(8, 8, 8, 8);
    pageLayout->setSpacing(6);

    auto* toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(6);

    auto* backButton =
        new QPushButton(QStringLiteral("Back to Editor"), page);
    backButton->setObjectName(
        QStringLiteral("insightFocusBackButton"));
    auto* dockButton =
        new QPushButton(QStringLiteral("Return to Dock"), page);
    dockButton->setObjectName(
        QStringLiteral("insightFocusReturnDockButton"));
    titleLabel = new QLabel(page);
    titleLabel->setObjectName(
        QStringLiteral("insightFocusTitle"));
    fitButton = new QPushButton(page);
    fitButton->setObjectName(
        QStringLiteral("insightFocusFitButton"));
    zoomOutButton = new QPushButton(page);
    zoomOutButton->setObjectName(
        QStringLiteral("insightFocusZoomOutButton"));
    zoomInButton = new QPushButton(page);
    zoomInButton->setObjectName(
        QStringLiteral("insightFocusZoomInButton"));
    searchEdit = new QLineEdit(page);
    searchEdit->setObjectName(
        QStringLiteral("insightFocusSearchEdit"));
    searchEdit->setPlaceholderText(
        QStringLiteral("Search current insight"));
    inspectorButton =
        new QPushButton(QStringLiteral("Inspector"), page);
    inspectorButton->setObjectName(
        QStringLiteral("insightFocusInspectorButton"));
    fitAction = createGraphViewAction(
        QString::fromLatin1(
            ActionIds::GraphViewFit));
    zoomOutAction = createGraphViewAction(
        QString::fromLatin1(
            ActionIds::GraphViewZoomOut));
    zoomInAction = createGraphViewAction(
        QString::fromLatin1(
            ActionIds::GraphViewZoomIn));
    bindGraphViewButton(fitButton, fitAction);
    bindGraphViewButton(
        zoomOutButton,
        zoomOutAction);
    bindGraphViewButton(
        zoomInButton,
        zoomInAction);
    refreshGraphViewActionAvailability(nullptr);

    toolbar->addWidget(backButton);
    toolbar->addWidget(dockButton);
    toolbar->addWidget(titleLabel, 1);
    toolbar->addWidget(fitButton);
    toolbar->addWidget(zoomOutButton);
    toolbar->addWidget(zoomInButton);
    toolbar->addWidget(searchEdit, 1);
    toolbar->addWidget(inspectorButton);
    pageLayout->addLayout(toolbar);

    contentHost = new QWidget(page);
    contentHost->setObjectName(
        QStringLiteral("insightFocusContentHost"));
    contentLayout = new QVBoxLayout(contentHost);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    pageLayout->addWidget(contentHost, 1);
    stack->addWidget(page);

    QObject::connect(backButton,
                     &QPushButton::clicked,
                     this,
                     [this]() { leaveToEditor(); });
    QObject::connect(dockButton,
                     &QPushButton::clicked,
                     this,
                     [this]() { returnToDock(); });
    QObject::connect(inspectorButton,
                     &QPushButton::clicked,
                     this,
                     [this]() {
                         PanelEntry* entry = activeEntry();
                         if (entry
                             && entry->registration.showInspector) {
                             entry->registration.showInspector();
                         }
                     });
    QObject::connect(searchEdit,
                     &QLineEdit::textChanged,
                     this,
                     [this](const QString& text) {
                         if (syncingSearch)
                             return;
                         PanelEntry* entry = activeEntry();
                         if (entry
                             && entry->registration.setSearchText) {
                             entry->registration.setSearchText(text);
                         }
                     });

    auto* escape = new QShortcut(
        QKeySequence(Qt::Key_Escape), page);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    QObject::connect(escape,
                     &QShortcut::activated,
                     this,
                     [this]() { leaveToEditor(); });
}

InsightFocusController::~InsightFocusController()
{
    restoreActivePanel(ActiveDockRestore::PriorVisibility);
}

bool InsightFocusController::registerPanel(
    const InsightFocusPanelRegistration& registration)
{
    const QString id = registration.id.trimmed();
    if (!stack
        || !page
        || id.isEmpty()
        || panels.contains(id)
        || !registration.dock
        || !registration.dock->widget()) {
        return false;
    }

    PanelEntry entry;
    entry.registration = registration;
    entry.registration.id = id;
    entry.dock = registration.dock;
    entry.panelWidget = registration.dock->widget();

    QPushButton* button =
        new QPushButton(QStringLiteral("Focus View"),
                        entry.panelWidget);
    button->setObjectName(
        QStringLiteral("insightFocusEnter.%1").arg(id));
    button->setToolTip(
        QStringLiteral("Open %1 in the main editor area")
            .arg(registration.title));
    if (auto* box =
            qobject_cast<QBoxLayout*>(
                entry.panelWidget->layout())) {
        box->insertWidget(1, button, 0, Qt::AlignRight);
    } else if (entry.panelWidget->layout()) {
        entry.panelWidget->layout()->addWidget(button);
    } else {
        auto* layout = new QVBoxLayout(entry.panelWidget);
        layout->addWidget(button, 0, Qt::AlignRight);
    }
    entry.enterButton = button;
    QObject::connect(button,
                     &QPushButton::clicked,
                     this,
                     [this, id]() { enter(id); });

    panels.insert(id, std::move(entry));
    return true;
}

bool InsightFocusController::enter(const QString& panelId)
{
    const QString id = panelId.trimmed();
    auto found = panels.find(id);
    if (found == panels.end() || !stack || !page || !contentLayout)
        return false;

    if (activePanelId == id) {
        stack->setCurrentWidget(page);
        return true;
    }


    PanelEntry& entry = found.value();
    QDockWidget* dock = entry.dock.data();
    QWidget* panel = dock ? dock->widget() : nullptr;
    if (!dock || !panel)
        return false;

    if (activePanelId.isEmpty())
        captureAndHideDocks();
    else
        restoreActivePanel(ActiveDockRestore::Hidden, false);

    entry.panelWidget = panel;
    expandPanelForFocus(entry, panel);
    dock->hide();
    dock->setProperty("insightFocusActive", true);
    dock->setWidget(nullptr);
    panel->setParent(contentHost);
    contentLayout->addWidget(panel, 1);
    panel->show();
    if (entry.enterButton)
        entry.enterButton->hide();

    activePanelId = id;
    updateToolbar(entry);
    stack->setCurrentWidget(page);
    page->show();
    return true;
}

void InsightFocusController::leaveToEditor()
{
    restoreActivePanel(ActiveDockRestore::PriorVisibility);
}

void InsightFocusController::returnToDock()
{
    restoreActivePanel(ActiveDockRestore::Visible);
}

bool InsightFocusController::isFocused() const
{
    return !activePanelId.isEmpty()
        && focusedPanelWidget() != nullptr;
}

QString InsightFocusController::focusedPanelId() const
{
    return activePanelId;
}

QWidget* InsightFocusController::focusedPanelWidget() const
{
    const PanelEntry* entry = activeEntry();
    return entry ? entry->panelWidget.data() : nullptr;
}

QWidget* InsightFocusController::focusPage() const
{
    return page;
}

InsightFocusController::PanelEntry*
InsightFocusController::activeEntry()
{
    auto found = panels.find(activePanelId);
    return found == panels.end() ? nullptr : &found.value();
}

const InsightFocusController::PanelEntry*
InsightFocusController::activeEntry() const
{
    auto found = panels.constFind(activePanelId);
    return found == panels.cend() ? nullptr : &found.value();
}

void InsightFocusController::captureAndHideDocks()
{
    savedDockVisibility.clear();
    savedMainWindow.clear();
    savedMainWindowState.clear();
    QWidget* hostWindow =
        stack ? stack->window() : nullptr;
    if (!hostWindow)
        return;

    savedMainWindow =
        qobject_cast<QMainWindow*>(hostWindow);
    if (savedMainWindow)
        savedMainWindowState = savedMainWindow->saveState();

    const QList<QDockWidget*> docks =
        hostWindow->findChildren<QDockWidget*>(
            QString(),
            Qt::FindDirectChildrenOnly);
    savedDockVisibility.reserve(docks.size());
    for (QDockWidget* dock : docks) {
        if (!dock)
            continue;
        savedDockVisibility.append(
            {dock, dock->isVisible()});
        dock->setProperty(
            "panelLayoutVisibilityBeforeTransient",
            dock->toggleViewAction()->isChecked());
        dock->setProperty(
            "panelLayoutHeightBeforeTransient",
            dock->height());
        dock->setProperty(
            "panelLayoutVisibilityTransient",
            true);
        dock->hide();
    }
}

void InsightFocusController::restoreDockVisibility(
    QDockWidget* activeDock,
    ActiveDockRestore activeDockRestore)
{
    const bool mainWindowRestored =
        savedMainWindow
        && !savedMainWindowState.isEmpty()
        && savedMainWindow->restoreState(
            savedMainWindowState);

    bool activeDockRecorded = false;
    bool activeDockWasVisible = false;
    for (const DockVisibility& state
         : std::as_const(savedDockVisibility)) {
        if (state.dock == activeDock) {
            activeDockRecorded = true;
            activeDockWasVisible = state.visible;
        }
        if (!mainWindowRestored && state.dock) {
            state.dock->setVisible(state.visible);
        }
    }

    if (activeDock
        && !(mainWindowRestored
             && activeDockRestore
                    == ActiveDockRestore::PriorVisibility)) {
        bool showActiveDock = activeDockWasVisible;
        if (!activeDockRecorded)
            showActiveDock = false;
        if (activeDockRestore
            == ActiveDockRestore::Hidden) {
            showActiveDock = false;
        } else if (activeDockRestore
                   == ActiveDockRestore::Visible) {
            showActiveDock = true;
        }
        activeDock->setVisible(showActiveDock);
        if (showActiveDock
            && activeDockRestore
                   == ActiveDockRestore::Visible) {
            activeDock->raise();
        }
    }

    for (const DockVisibility& state
         : std::as_const(savedDockVisibility)) {
        if (state.dock) {
            state.dock->setProperty(
                "panelLayoutVisibilityTransient",
                false);
            state.dock->setProperty(
                "panelLayoutVisibilityBeforeTransient",
                QVariant());
            state.dock->setProperty(
                "panelLayoutHeightBeforeTransient",
                QVariant());
        }
    }
    savedDockVisibility.clear();
    savedMainWindow.clear();
    savedMainWindowState.clear();
}

void InsightFocusController::restoreActivePanel(
    ActiveDockRestore activeDockRestore,
    bool restoreDocks)
{
    PanelEntry* entry = activeEntry();
    if (!entry) {
        activePanelId.clear();
        refreshGraphViewActionAvailability(nullptr);
        if (restoreDocks)
            restoreDockVisibility(
                nullptr,
                ActiveDockRestore::PriorVisibility);
        if (restoreDocks && stack && editor)
            stack->setCurrentWidget(editor);
        return;
    }

    QWidget* panel = entry->panelWidget;
    QDockWidget* dock = entry->dock.data();
    if (panel && dock) {
        panel->hide();
        if (contentLayout)
            contentLayout->removeWidget(panel);
        panel->setParent(dock);
        dock->setWidget(panel);
        dock->setProperty("insightFocusActive", false);
        restorePanelConstraints(*entry, panel);
        panel->show();
        if (entry->enterButton)
            entry->enterButton->show();
        dock->hide();
    }

    activePanelId.clear();
    refreshGraphViewActionAvailability(nullptr);
    if (restoreDocks)
        restoreDockVisibility(dock, activeDockRestore);
    if (restoreDocks && stack && editor)
        stack->setCurrentWidget(editor);
}

void InsightFocusController::expandPanelForFocus(
    PanelEntry& entry,
    QWidget* panel)
{
    if (!panel || entry.constraintsSaved)
        return;

    entry.savedMinimumHeight = panel->minimumHeight();
    entry.savedMaximumHeight = panel->maximumHeight();
    entry.savedSizePolicy = panel->sizePolicy();
    entry.constraintsSaved = true;

    panel->setMinimumHeight(0);
    panel->setMaximumHeight(QWIDGETSIZE_MAX);
    QSizePolicy policy = panel->sizePolicy();
    policy.setVerticalPolicy(QSizePolicy::Expanding);
    panel->setSizePolicy(policy);
}

void InsightFocusController::restorePanelConstraints(
    PanelEntry& entry,
    QWidget* panel)
{
    if (!panel || !entry.constraintsSaved)
        return;

    panel->setMinimumHeight(entry.savedMinimumHeight);
    panel->setMaximumHeight(entry.savedMaximumHeight);
    panel->setSizePolicy(entry.savedSizePolicy);
    entry.constraintsSaved = false;
}

void InsightFocusController::updateToolbar(
    const PanelEntry& entry)
{
    if (titleLabel) {
        titleLabel->setText(
            QStringLiteral("%1 — Focus View")
                .arg(entry.registration.title));
    }
    refreshGraphViewActionAvailability(&entry);
    if (inspectorButton) {
        inspectorButton->setEnabled(
            static_cast<bool>(
                entry.registration.showInspector));
    }
    if (searchEdit) {
        searchEdit->setEnabled(
            static_cast<bool>(
                entry.registration.setSearchText));
        const QString text = entry.registration.searchText
            ? entry.registration.searchText()
            : QString();
        syncingSearch = true;
        {
            const QSignalBlocker blocker(searchEdit);
            searchEdit->setText(text);
        }
        syncingSearch = false;
    }
}
