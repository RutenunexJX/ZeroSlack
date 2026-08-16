#include "globalcontrolcoordinator.h"

#include "actionregistry.h"
#include "globalcontrolpanel.h"

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QWidget>
#include <utility>

GlobalControlCoordinator::GlobalControlCoordinator(QWidget* anchorWidget,
                                                   QObject* parent)
    : QObject(parent)
    , anchor(anchorWidget)
{
    panel = std::make_unique<GlobalControlPanel>(anchorWidget);
    panel->setQueryChangedHandler(
        [this](const QString& text) { refresh(text); });
    panel->setCategoryChangedHandler(
        [this](GlobalControlCategory) {
            refresh(panel ? panel->queryText() : QString());
        });
    panel->setItemActivatedHandler(
        [this](const GlobalControlItem& item) { dispatch(item); });
}

GlobalControlCoordinator::~GlobalControlCoordinator()
{
    if (installed && qApp)
        qApp->removeEventFilter(this);
}

void GlobalControlCoordinator::setActionHandler(
    std::function<void(const GlobalControlItem&)> handler)
{
    actionHandler = std::move(handler);
}

void GlobalControlCoordinator::setOpeningHandler(
    std::function<void()> handler)
{
    openingHandler = std::move(handler);
}

void GlobalControlCoordinator::setContextProvider(
    std::function<GlobalControlQueryContext()> provider)
{
    contextProvider = std::move(provider);
}

void GlobalControlCoordinator::setItemProvider(
    std::function<QList<GlobalControlItem>(
        GlobalControlCategory,
        const QString&,
        const GlobalControlQueryContext&)> provider)
{
    itemProvider = std::move(provider);
}

void GlobalControlCoordinator::setOpenRequestHandler(
    std::function<bool()> handler)
{
    openRequestHandler = std::move(handler);
}

void GlobalControlCoordinator::install()
{
    if (installed || !qApp)
        return;
    qApp->installEventFilter(this);
    installed = true;
}

bool GlobalControlCoordinator::eventFilter(QObject*, QEvent* event)
{
    return handleKeyEvent(event);
}

bool GlobalControlCoordinator::handleKeyEvent(QEvent* event)
{
    if (event->type() != QEvent::KeyPress)
        return false;

    auto* keyEvent = static_cast<QKeyEvent*>(event);
    if (keyEvent->isAutoRepeat())
        return false;

    const QString shortcutText =
        effectiveActionShortcut(
            QString::fromLatin1(
                ActionIds::ViewGlobalControl));
    const QKeySequence shortcut =
        QKeySequence::fromString(
            shortcutText,
            QKeySequence::PortableText);
    if (shortcutText.isEmpty()
        || shortcut.matches(
               QKeySequence(
                   keyEvent->keyCombination()))
               != QKeySequence::ExactMatch) {
        return false;
    }

    const bool routed = openRequestHandler
        && openRequestHandler();
    if (!routed)
        open();
    keyEvent->accept();
    return true;
}

void GlobalControlCoordinator::open()
{
    if (!panel)
        return;
    if (panel->isVisible()) {
        panel->focusSearch();
        return;
    }
    currentContext = contextProvider
        ? contextProvider() : GlobalControlQueryContext();
    panel->setCategory(currentContext.editorAvailable
                           ? GlobalControlCategory::Symbols
                           : GlobalControlCategory::Commands);
    if (openingHandler)
        openingHandler();
    refresh();
    panel->showCentered(anchor);
}

void GlobalControlCoordinator::refresh(const QString& queryText)
{
    if (!panel)
        return;
    const GlobalControlCategory category = panel->category();
    panel->setItems(
        category == GlobalControlCategory::Commands
            ? service.query(category, queryText, currentContext)
            : (itemProvider
                   ? itemProvider(category, queryText, currentContext)
                   : QList<GlobalControlItem>()));
}

void GlobalControlCoordinator::dispatch(const GlobalControlItem& item)
{
    if (actionHandler)
        actionHandler(item);
}
