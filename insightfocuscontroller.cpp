#include "insightfocuscontroller.h"

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
    fitButton = new QPushButton(QStringLiteral("Fit"), page);
    fitButton->setObjectName(
        QStringLiteral("insightFocusFitButton"));
    zoomOutButton = new QPushButton(QStringLiteral("-"), page);
    zoomOutButton->setObjectName(
        QStringLiteral("insightFocusZoomOutButton"));
    zoomInButton = new QPushButton(QStringLiteral("+"), page);
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
    QObject::connect(fitButton,
                     &QPushButton::clicked,
                     this,
                     [this]() {
                         PanelEntry* entry = activeEntry();
                         if (entry && entry->registration.fit)
                             entry->registration.fit();
                     });
    QObject::connect(zoomOutButton,
                     &QPushButton::clicked,
                     this,
                     [this]() {
                         PanelEntry* entry = activeEntry();
                         if (entry && entry->registration.zoomOut)
                             entry->registration.zoomOut();
                     });
    QObject::connect(zoomInButton,
                     &QPushButton::clicked,
                     this,
                     [this]() {
                         PanelEntry* entry = activeEntry();
                         if (entry && entry->registration.zoomIn)
                             entry->registration.zoomIn();
                     });
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
    restoreActivePanel(false);
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
    QDockWidget* dock = entry.registration.dock;
    QWidget* panel = dock ? dock->widget() : nullptr;
    if (!dock || !panel)
        return false;

    if (activePanelId.isEmpty())
        captureAndHideDocks();
    else
        restoreActivePanel(false, false);

    entry.panelWidget = panel;
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
    restoreActivePanel(false);
}

void InsightFocusController::returnToDock()
{
    restoreActivePanel(true);
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
    QWidget* hostWindow =
        stack ? stack->window() : nullptr;
    if (!hostWindow)
        return;

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
        dock->hide();
    }
}

void InsightFocusController::restoreDockVisibility(
    QDockWidget* activeDock,
    bool showActiveDock)
{
    bool activeDockRecorded = false;
    for (const DockVisibility& state :
         std::as_const(savedDockVisibility)) {
        QDockWidget* dock = state.dock;
        if (!dock)
            continue;
        if (dock == activeDock) {
            activeDockRecorded = true;
            dock->setVisible(showActiveDock);
        } else {
            dock->setVisible(state.visible);
        }
    }
    if (activeDock && !activeDockRecorded)
        activeDock->setVisible(showActiveDock);
    if (activeDock && showActiveDock)
        activeDock->raise();
    savedDockVisibility.clear();
}

void InsightFocusController::restoreActivePanel(
    bool showDock,
    bool restoreDocks)
{
    PanelEntry* entry = activeEntry();
    if (!entry) {
        activePanelId.clear();
        if (restoreDocks)
            restoreDockVisibility(nullptr, false);
        if (restoreDocks && stack && editor)
            stack->setCurrentWidget(editor);
        return;
    }

    QWidget* panel = entry->panelWidget;
    QDockWidget* dock = entry->registration.dock;
    if (panel && dock) {
        panel->hide();
        if (contentLayout)
            contentLayout->removeWidget(panel);
        panel->setParent(dock);
        dock->setWidget(panel);
        dock->setProperty("insightFocusActive", false);
        panel->show();
        if (entry->enterButton)
            entry->enterButton->show();
        dock->hide();
    }

    activePanelId.clear();
    if (restoreDocks)
        restoreDockVisibility(dock, showDock);
    if (restoreDocks && stack && editor)
        stack->setCurrentWidget(editor);
}

void InsightFocusController::updateToolbar(
    const PanelEntry& entry)
{
    if (titleLabel) {
        titleLabel->setText(
            QStringLiteral("%1 — Focus View")
                .arg(entry.registration.title));
    }
    if (fitButton)
        fitButton->setEnabled(
            static_cast<bool>(entry.registration.fit));
    if (zoomOutButton) {
        zoomOutButton->setEnabled(
            static_cast<bool>(entry.registration.zoomOut));
    }
    if (zoomInButton) {
        zoomInButton->setEnabled(
            static_cast<bool>(entry.registration.zoomIn));
    }
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
