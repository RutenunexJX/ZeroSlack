#include "workspaceswitcher.h"
#include "workspacemanager.h"
#include "workspacesessioncoordinator.h"
#include "tabmanager.h"
#include "editorfileidentity.h"
#include "roundedicons.h"
#include "uicontrols.h"
#include "uitypography.h"
#include <QApplication>
#include <QDir>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QScreen>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

WorkspaceSwitcher::WorkspaceSwitcher(WorkspaceManager* workspaces, TabManager* tabs,
                                     WorkspaceSessionCoordinator* sessions,
                                     std::function<void()> openWorkspace, QWidget* parent)
    : QObject(parent), workspaces(workspaces), tabs(tabs), sessions(sessions),
      openWorkspace(std::move(openWorkspace)), popup(UiControls::menu(parent))
{
    popup->setObjectName(QStringLiteral("workspaceSwitcherPopup"));
    popup->installEventFilter(this);
    connect(workspaces, &WorkspaceManager::workspaceListChanged, this, &WorkspaceSwitcher::refresh);
    connect(workspaces, &WorkspaceManager::workspaceActivated, this, &WorkspaceSwitcher::refresh);
    connect(workspaces, &WorkspaceManager::workspaceClosed, this, &WorkspaceSwitcher::refresh);
    connect(tabs, &TabManager::workspaceSessionStateChanged, this, &WorkspaceSwitcher::refresh);
}

QToolButton* WorkspaceSwitcher::createButton(QWidget* parent, const QString& objectName)
{
    auto* button = UiControls::toolButton(parent);
    button->setObjectName(objectName);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    button->setMinimumWidth(72);
    button->setFocusPolicy(Qt::StrongFocus);
    button->installEventFilter(this);
    buttons.append(button);
    connect(button, &QToolButton::clicked, this, [this, button] {
        if (popup->isVisible()) popup->hide();
        else showPopup(button);
    });
    updateButton(button);
    return button;
}

void WorkspaceSwitcher::updateButton(QToolButton* button)
{
    if (!button || !workspaces) return;
    const QString name = workspaces->isWorkspaceOpen() ? workspaces->getWorkspaceAlias()
                                                       : tr("Workspaces");
    button->setText(button->fontMetrics().elidedText(name, Qt::ElideRight,
                    qMax(30, button->width() - 30)) + QStringLiteral("  ▾"));
    button->setAccessibleName(tr("Switch workspace: %1").arg(name));
    button->setToolTip(tr("%1\n%2\n%3 open workspace(s)")
        .arg(name, QDir::toNativeSeparators(workspaces->getWorkspacePath()))
        .arg(workspaces->workspaceEntries().size()));
}

void WorkspaceSwitcher::refresh()
{
    for (const auto& button : buttons) updateButton(button);
}

void WorkspaceSwitcher::requestWorkspace(const QString& path, bool close)
{
    popup->hide();
    QTimer::singleShot(0, this, [this, path, close] {
        if (!workspaces || !sessions) return;
        const auto entries = workspaces->workspaceEntries();
        for (int i = 0; i < entries.size(); ++i) {
            if (!EditorFileIdentity::same(entries.at(i).path, path)) continue;
            if (close) sessions->closeWorkspace(i);
            else sessions->switchWorkspace(i);
            return;
        }
        if (!close) sessions->openWorkspace(path);
    });
}

void WorkspaceSwitcher::showPopup(QToolButton* anchor)
{
    if (!workspaces || !anchor) return;
    popup->clear();
    const int width = qMin(420, anchor->screen()->availableGeometry().width() - 24);
    const auto entries = workspaces->workspaceEntries();
    const auto addEntry = [this, width](const WorkspaceManager::WorkspaceEntry& entry, bool opened) {
        auto* row = new QWidget(popup);
        row->setFixedWidth(width);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(6, 2, 6, 2);
        layout->setSpacing(4);
        auto* choice = UiControls::toolButton(row);
        choice->setToolButtonStyle(Qt::ToolButtonTextOnly);
        choice->setObjectName(QStringLiteral("workspaceChoice"));
        choice->installEventFilter(this);
        choice->setProperty("workspacePath", entry.path);
        choice->setAccessibleName(tr("Open workspace %1, %2").arg(entry.alias, entry.path));
        const bool active = EditorFileIdentity::same(entry.path, workspaces->getWorkspacePath());
        choice->setCheckable(true);
        choice->setChecked(active);
        auto* labels = new QVBoxLayout(choice);
        labels->setContentsMargins(10, 7, 10, 7);
        labels->setSpacing(3);
        QString caption = (active ? QStringLiteral("✓  ") : QString()) + entry.alias;
        if (opened && tabs && tabs->workspaceHasUnsavedChanges(entry.path)) caption += QStringLiteral("  ●");
        auto* name = UiControls::label(caption, choice);
        name->setTextFormat(Qt::PlainText);
        UiTypography::apply(name, UiTypography::Role::Section);
        auto* path = UiControls::label(QDir::toNativeSeparators(entry.path), choice);
        path->setTextFormat(Qt::PlainText);
        path->setWordWrap(true);
        UiTypography::apply(path, UiTypography::Role::Metadata);
        for (auto* label : {name, path}) label->setAttribute(Qt::WA_TransparentForMouseEvents);
        labels->addWidget(name);
        labels->addWidget(path);
        const int textWidth = width - (opened ? 78 : 38);
        name->setWordWrap(true);
        const auto wrappedHeight = [textWidth](QLabel* label) {
            return label->fontMetrics().boundingRect(QRect(0, 0, textWidth, 10000),
                                                     Qt::TextWordWrap, label->text()).height();
        };
        const int rowHeight = wrappedHeight(name) + wrappedHeight(path) + 21;
        choice->setFixedHeight(rowHeight);
        row->setFixedHeight(rowHeight + 4);
        choice->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        layout->addWidget(choice, 1);
        connect(choice, &QToolButton::clicked, this, [this, entry] { requestWorkspace(entry.path, false); });
        if (opened) {
            auto* close = UiControls::railButton(row);
            close->setObjectName(QStringLiteral("workspaceCloseButton"));
            close->installEventFilter(this);
            close->setProperty("workspacePath", entry.path);
            close->setIcon(RoundedIcons::icon(RoundedIcons::Close));
            close->setIconSize(QSize(14, 14));
            close->setToolTip(tr("Close workspace %1").arg(entry.alias));
            close->setAccessibleName(close->toolTip());
            layout->addWidget(close);
            connect(close, &QToolButton::clicked, this, [this, entry] { requestWorkspace(entry.path, true); });
        }
        auto* action = new QWidgetAction(popup);
        action->setDefaultWidget(row);
        popup->addAction(action);
        connect(action, &QAction::triggered, this, [this, entry] { requestWorkspace(entry.path, false); });
    };
    const auto addSection = [this, width](const QString& text) {
        auto* label = UiControls::label(text, popup);
        UiTypography::apply(label, UiTypography::Role::Metadata);
        label->setContentsMargins(14, 8, 10, 4);
        label->setFixedWidth(width);
        auto* section = new QWidgetAction(popup);
        section->setDefaultWidget(label);
        section->setEnabled(false);
        popup->addAction(section);
    };
    addSection(tr("Open · %1").arg(entries.size()));
    for (const auto& entry : entries) addEntry(entry, true);
    bool addedRecent = false;
    for (const auto& entry : workspaces->recentWorkspaceEntries()) {
        bool opened = false;
        for (const auto& current : entries) opened |= EditorFileIdentity::same(current.path, entry.path);
        if (opened) continue;
        if (!addedRecent) { addSection(tr("Recent")); addedRecent = true; }
        addEntry(entry, false);
    }
    popup->addSeparator();
    auto* open = popup->addAction(RoundedIcons::icon(RoundedIcons::Folder), tr("Open workspace…"));
    connect(open, &QAction::triggered, this, [this] {
        if (openWorkspace) QTimer::singleShot(0, this, openWorkspace);
    });
    popup->popup(anchor->mapToGlobal(QPoint(0, anchor->height())));
    for (auto* button : popup->findChildren<QToolButton*>(QStringLiteral("workspaceChoice"))) {
        if (button->isChecked()) { button->setFocus(Qt::PopupFocusReason); break; }
    }
}

bool WorkspaceSwitcher::eventFilter(QObject* object, QEvent* event)
{
    if (event->type() == QEvent::Resize) {
        if (auto* button = qobject_cast<QToolButton*>(object)) {
            if (buttons.contains(QPointer<QToolButton>(button))) updateButton(button);
        }
    }
    if (popup->isVisible() && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape) { popup->hide(); return true; }
        if (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down) {
            const auto choices = popup->findChildren<QToolButton*>(QStringLiteral("workspaceChoice"));
            int current = choices.size();
            for (int i = 0; i < choices.size(); ++i) {
                if (choices.at(i) == object || choices.at(i)->parentWidget() == object->parent()) {
                    current = i; break;
                }
            }
            const int next = (current + (key->key() == Qt::Key_Down ? 1 : -1)
                              + choices.size() + 1) % (choices.size() + 1);
            if (next < choices.size()) choices.at(next)->setFocus(Qt::TabFocusReason);
            else { popup->setFocus(); popup->setActiveAction(popup->actions().last()); }
            return true;
        }
    }
    return QObject::eventFilter(object, event);
}
