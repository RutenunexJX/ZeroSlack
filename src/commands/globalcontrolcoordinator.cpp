#include "globalcontrolcoordinator.h"

#include "actionregistry.h"
#include "documenttemplatemodel.h"
#include "globalcontrolpanel.h"
#include "mycodeeditor.h"

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
        [this](GlobalControlCategory category) {
            if (templateEditor)
                templateEditor->setTemplateSearchActive(
                    currentContext.simulationCommands || category == GlobalControlCategory::Templates);
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
        const GlobalControlQueryContext&, QString*)> provider)
{
    itemProvider = std::move(provider);
}

void GlobalControlCoordinator::setOpenRequestHandler(
    std::function<bool()> handler)
{
    openRequestHandler = std::move(handler);
}

void GlobalControlCoordinator::setAnchorPositionProvider(
    std::function<QPoint()> provider)
{
    anchorPositionProvider = std::move(provider);
}

void GlobalControlCoordinator::install()
{
    if (installed || !qApp)
        return;
    qApp->installEventFilter(this);
    installed = true;
}

bool GlobalControlCoordinator::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == panel.get() && event && event->type() == QEvent::Hide && templateEditor)
        templateEditor->setTemplateSearchActive(false);
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
    const QPoint popupAnchor = anchorPositionProvider
        ? anchorPositionProvider()
        : QPoint();
    currentContext = contextProvider
        ? contextProvider() : GlobalControlQueryContext();
    templateEditor = nullptr;
    if (currentContext.editorAvailable)
        for (auto* focus = QApplication::focusWidget(); focus && !templateEditor; focus = focus->parentWidget())
            templateEditor = qobject_cast<MyCodeEditor*>(focus);
    templateContext = currentContext;
    if (templateEditor && !currentContext.simulationCommands) {
        // Template insertion uses the captured selection, not the symbol token.
        const auto cursor = templateEditor->textCursor();
        templateContext.replacementStart = cursor.selectionStart();
        templateContext.replacementLength = cursor.selectionEnd() - cursor.selectionStart();
        templateContext.templateBoundaryError = templateContext.readOnly ? QString()
            : templateEditor->simulationTemplateInsertionError(
                templateContext.replacementStart, templateContext.replacementLength, {},
                templateContext.templateParentId, templateContext.templateParentSlot);
    }
    panel->setSimulationOnly(currentContext.simulationCommands);
    panel->setCategory(currentContext.editorAvailable
                           ? GlobalControlCategory::Symbols
                           : GlobalControlCategory::Commands);
    if (openingHandler)
        openingHandler();
    if (templateEditor)
        templateEditor->setTemplateSearchActive(
            currentContext.simulationCommands || panel->category() == GlobalControlCategory::Templates);
    panel->resetQuery(currentContext.initialQuery);
    refresh(currentContext.initialQuery);
    if (!popupAnchor.isNull())
        panel->showAt(anchor, popupAnchor);
    else
        panel->showCentered(anchor);
}

void GlobalControlCoordinator::refresh(const QString& queryText)
{
    if (!panel)
        return;
    const GlobalControlCategory category = currentContext.simulationCommands ? GlobalControlCategory::Templates : panel->category();
    const auto& context = category == GlobalControlCategory::Templates
        ? templateContext : currentContext;
    QString notice;
    const auto items =
        category == GlobalControlCategory::Commands
            ? service.query(category, queryText, currentContext)
            : (itemProvider
                   ? itemProvider(category, queryText.startsWith('@') ? queryText.mid(1) : queryText, context, &notice)
                   : QList<GlobalControlItem>());
    panel->setItems(items, notice);
}

void GlobalControlCoordinator::dispatch(const GlobalControlItem& item)
{
    if (templateEditor) templateEditor->setTemplateSearchActive(false);
    if (actionHandler)
        actionHandler(item);
}

void GlobalControlCoordinator::refreshVisibleItems()
{
    if (panel && panel->isVisible()) refresh(panel->queryText());
}
