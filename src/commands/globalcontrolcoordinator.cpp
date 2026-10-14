#include "globalcontrolcoordinator.h"

#include "actionregistry.h"
#include "globalcontrolpanel.h"
#include "mycodeeditor.h"
#include "documenttemplatemodel.h"
#include "simulationcommandprefix.h"

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QWidget>
#include <QTimer>
#include <QTextDocument>
#include <utility>

GlobalControlCoordinator::GlobalControlCoordinator(QWidget* anchorWidget,
                                                   QObject* parent)
    : QObject(parent)
    , anchor(anchorWidget)
{
    panel = std::make_unique<GlobalControlPanel>(anchorWidget);
    panel->setQueryChangedHandler(
        [this](const QString& text) { if (!restoreNativePrefix(text)) refresh(text); });
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
    if (handleTemplateEvent(watched, event)) return true;
    return handleKeyEvent(event);
}

bool GlobalControlCoordinator::handleTemplateEvent(QObject* watched, QEvent* event)
{
    if (!event) return false;
    if (event->type() == QEvent::ApplicationDeactivate || event->type() == QEvent::WindowDeactivate) {
        f24Held = false;
        toggleSpaceHeld = false;
        return false;
    }
    if (event->type() == QEvent::FocusOut && watched == templateEditor) {
        QTimer::singleShot(0, this, [this] {
            const auto* focus = QApplication::focusWidget();
            if (!templateEditor || (focus != templateEditor && !(panel->isVisible() && panel->isAncestorOf(focus)))) f24Held = false;
        });
    }
    if (watched == panel.get() && event->type() == QEvent::Hide) {
        nativePrefixPending = false;
        if (templateEditor) templateEditor->setTemplateSearchActive(false);
    }
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease && event->type() != QEvent::ShortcutOverride) return false;
    auto* key = static_cast<QKeyEvent*>(event);
    auto* focus = QApplication::focusWidget();
    auto* editor = qobject_cast<MyCodeEditor*>(focus);
    const bool inPalette = panel->isVisible() && (focus == panel.get() || panel->isAncestorOf(focus));
    if (inPalette) editor = templateEditor;
    if (QApplication::activeModalWidget() || !editor || !anchor || editor->window() != anchor->window()
        || (QApplication::activePopupWidget() && QApplication::activePopupWidget() != panel.get())) {
        f24Held = false;
        return false;
    }
    const bool locked = editor->templateAuthoringLocked();
    if (event->type() == QEvent::ShortcutOverride) {
        if (key->key() == Qt::Key_F24 || (f24Held && key->key() == Qt::Key_Space) || (locked && key->key() == Qt::Key_Escape)) { key->accept(); return true; }
        return false;
    }
    if (key->key() == Qt::Key_F24) {
        if (!key->isAutoRepeat()) {
            f24Held = event->type() == QEvent::KeyPress;
            if (f24Held) templateEditor = editor;
        }
        key->accept(); return true;
    }
    if (key->key() == Qt::Key_Space && toggleSpaceHeld) {
        if (event->type() == QEvent::KeyRelease && !key->isAutoRepeat()) toggleSpaceHeld = false;
        key->accept(); return true;
    }
    if (event->type() != QEvent::KeyPress) return false;
    if (f24Held && key->key() == Qt::Key_Space) {
        if (!key->isAutoRepeat()) {
            panel->hide(); nativePrefixPending = false;
            editor->setTemplateAuthoringLocked(!locked); editor->setFocus();
            f24Held = false;
            toggleSpaceHeld = true;
        }
        key->accept(); return true;
    }
    if (locked && key->key() == Qt::Key_Escape) {
        nativePrefixPending = false; f24Held = false; panel->hide(); editor->setTemplateAuthoringLocked(false);
        key->accept(); return true;
    }
    if (inPalette) return false;
    const bool text = !(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) && !key->text().isEmpty()
        && key->key() != Qt::Key_Return && key->key() != Qt::Key_Enter && key->key() != Qt::Key_Tab && key->key() != Qt::Key_Backspace;
    if (text && (f24Held || (locked && !editor->templateInputEditing()))) {
        openTemplates(editor, key->text()); key->accept(); return true;
    }
    if (text && locked && key->text() == "@" && editor->templateCodeSegmentEmpty()) {
        openTemplates(editor, "@", true); key->accept(); return true;
    }
    return false;
}

void GlobalControlCoordinator::openTemplates(MyCodeEditor* editor, const QString& query, bool rawPrefix)
{
    if (!editor || !panel) return;
    templateEditor = editor;
    currentContext = contextProvider ? contextProvider() : GlobalControlQueryContext();
    if (!currentContext.editorAvailable) return;
    currentContext.simulationCommands = true;
    currentContext.readOnly = editor->documentReadOnly();
    currentContext.replacementStart = editor->textCursor().selectionStart();
    currentContext.replacementLength = editor->textCursor().selectionEnd() - currentContext.replacementStart;
    editor->templateInsertionContext(&currentContext.templateParentId, &currentContext.templateParentSlot);
    currentContext.templateContent = editor->templateContainerContent();
    currentContext.templateBoundaryError = currentContext.readOnly ? QString()
        : editor->simulationTemplateInsertionError(currentContext.replacementStart,
            currentContext.replacementLength, {}, currentContext.templateParentId, currentContext.templateParentSlot);
    nativePrefixPending = rawPrefix;
    if (openingHandler) openingHandler();
    editor->setTemplateSearchActive(true);
    panel->setSimulationOnly(true); panel->setCategory(GlobalControlCategory::Templates);
    panel->resetQuery(query); refresh(query);
    panel->showAt(anchor, editor->viewport()->mapToGlobal(editor->cursorRect().bottomLeft()));
    panel->focusSearch(false);
}

bool GlobalControlCoordinator::restoreNativePrefix(const QString& query)
{
    if (!nativePrefixPending || !templateEditor || !SimulationCommandPrefix::nativeEventInput(query)) return false;
    nativePrefixPending = false;
    auto* editor = templateEditor.data();
    panel->hide(); editor->setTemplateSearchActive(false); editor->setFocus();
    if (editor->cachedDocumentText() != currentContext.documentText || editor->textCursor().position() != currentContext.cursorPosition) {
        emit editor->editorStatusMessageRequested(QStringLiteral("The input target changed; reopen the template search.")); return true;
    }
    auto input = editor->beginUserEdit(-1);
    editor->insertPlainText(query);
    return true;
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
    if (auto* editor = qobject_cast<MyCodeEditor*>(QApplication::focusWidget()); editor && editor->templateAuthoringLocked()) {
        openTemplates(editor, {}); return;
    }
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
    panel->setSimulationOnly(currentContext.simulationCommands);
    panel->setCategory(currentContext.editorAvailable
                           ? GlobalControlCategory::Symbols
                           : GlobalControlCategory::Commands);
    if (openingHandler)
        openingHandler();
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
    QString notice;
    const auto items =
        category == GlobalControlCategory::Commands
            ? service.query(category, queryText, currentContext)
            : (itemProvider
                   ? itemProvider(category, queryText.startsWith('@') ? queryText.mid(1) : queryText, currentContext, &notice)
                   : QList<GlobalControlItem>());
    panel->setItems(items, notice);
}

void GlobalControlCoordinator::dispatch(const GlobalControlItem& item)
{
    nativePrefixPending = false;
    if (templateEditor) templateEditor->setTemplateSearchActive(false);
    if (actionHandler)
        actionHandler(item);
}

void GlobalControlCoordinator::refreshVisibleItems()
{
    if (panel && panel->isVisible()) refresh(panel->queryText());
}
