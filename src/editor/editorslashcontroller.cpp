#include "editorslashcontroller.h"
#include "mycodeeditor.h"
#include "editorruntime.h"
#include "compactcandidatedelegate.h"
#include "insightvisualstyle.h"
#include "uicontrols.h"
#include "templateinputservice.h"
#include <QApplication>
#include <QFrame>
#include <QLineEdit>
#include <QListWidget>
#include <QLabel>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <QScreen>
#include <QTimer>
#include <QSignalBlocker>
#include <QVBoxLayout>

EditorSlashController::EditorSlashController(MyCodeEditor* e, EditorModeController* m)
    : QObject(e), editor(e), modes(m) {
    pendingRefresh = new QTimer(this); pendingRefresh->setInterval(80);
    connect(pendingRefresh, &QTimer::timeout, this, [this] {
        if (!searching) { pendingRefresh->stop(); return; }
        if (!current() || UserTemplateService::getInstance()->catalogRevision() != catalogRevision) { cancel(true); return; }
        filter();
    });
    connect(editor, &MyCodeEditor::fileNameChanged, this, [this] { cancel(); });
    connect(editor, &MyCodeEditor::documentChangeApplied, this, [this] { cancel(); });
    connect(editor, &QPlainTextEdit::cursorPositionChanged, this, [this] { if (searching && !current()) cancel(); });
    connect(editor, &MyCodeEditor::editorModeStateChanged, this, [this] {
        if (searching && !modes->isActive(EditorModeId::SlashSearch)) cancel();
    });
    qApp->installEventFilter(this);
}

void EditorSlashController::ensurePopup() {
    if (popup) return;
    popup = new QFrame(editor, Qt::Popup | Qt::FramelessWindowHint);
    popup->setObjectName("editorSlashPopup");
    InsightVisualStyle::applyGlobalControlPanel(popup);
    auto* layout = new QVBoxLayout(popup);
    layout->setContentsMargins(6, 6, 6, 6);
    query = UiControls::lineEdit(popup); query->setObjectName("editorSlashQuery");
    query->setPlaceholderText("Search templates and blocks");
    list = UiControls::listWidget(popup); list->setObjectName("editorSlashCandidates");
    list->setItemDelegate(new CompactCandidateDelegate(list));
    list->setFocusPolicy(Qt::NoFocus);
    feedback = UiControls::label("Enter inserts · Esc cancels", popup);
    feedback->setWordWrap(true);
    layout->addWidget(query); layout->addWidget(list); layout->addWidget(feedback);
    connect(query, &QLineEdit::textChanged, this, [this] { if (searching) filter(); });
    connect(list, &QListWidget::currentRowChanged, this, [this] { if (searching) showEligibility(); });
    connect(list, &QListWidget::itemActivated, this, [this] { confirm(); });
}

EditorSlashController::~EditorSlashController() { qApp->removeEventFilter(this); delete popup; }
bool EditorSlashController::active() const { return searching; }
bool EditorSlashController::current() const {
    return source && source == editor->document() && revision == editor->semanticDocumentRevision()
        && file == editor->documentFileName() && original == editor->textCursor();
}

bool EditorSlashController::trigger(QKeyEvent* event) {
    if (!event || event->text() != "/" || (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) return false;
    if (editor->documentReadOnly()) return false;
    if (editor->columnSelectionActive() || modes->isActive(EditorModeId::MultiCursor)
        || modes->isActive(EditorModeId::SignalSelection) || modes->isActive(EditorModeId::VirtualCursor)) {
        emit editor->editorStatusMessageRequested("Finish the current multi-target input before opening resource search.");
        return false;
    }
    if (!editor->canApplyInsertion()) return false;
    ensurePopup();
    source = editor->document(); original = editor->textCursor();
    revision = editor->semanticDocumentRevision(); file = editor->documentFileName();
    auto* service = UserTemplateService::getInstance();
    snapshot = OrdinaryTemplatePlanner::catalog(service, file);
    for (const auto& entry:QList<QStringList>{{"note","Text note","text"},{"image","Image","image"},{"reference","Object reference","reference"},{"code","Code block","code"},{"attachment","Attachment","attachment"},{"wave","Wave","wave"}}) {
        UserTemplateRecord action; action.id="block:"+entry[0]; action.commandToken="/"+entry[0]; action.label=entry[1];
        action.command={{"kind","block"},{"blockType",entry[2]}}; action.storageSource="block-action";
        snapshot.append(action);
    }
    catalogRevision = service->catalogRevision();
    modes->enter(EditorModeId::SlashSearch, EditorModeEntryReason::UserAction);
    searching = true; preedit = false; compositionCommit = false;
    query->clear(); filter();
    const QRect bounds = editor->screen()->availableGeometry();
    const int width = qMin(480, bounds.width());
    popup->resize(width, qMin(320, bounds.height()));
    QPoint position = editor->viewport()->mapToGlobal(editor->cursorRect().bottomLeft());
    position.setX(qBound(bounds.left(), position.x(), bounds.right() - popup->width() + 1));
    position.setY(qBound(bounds.top(), position.y(), bounds.bottom() - popup->height() + 1));
    popup->move(position); popup->show(); query->setFocus(Qt::PopupFocusReason);
    return true;
}

void EditorSlashController::cancel(bool restoreFocus) {
    if (!searching) return;
    searching = false;
    pendingRefresh->stop();
    popup->hide(); modes->exit(EditorModeId::SlashSearch, EditorModeExitReason::Canceled);
    source.clear(); snapshot.clear(); matches.clear();
    if (restoreFocus && editor->isVisible()) {
        editor->window()->activateWindow();
        editor->setFocus(Qt::OtherFocusReason);
    }
}

void EditorSlashController::filter() {
    const int selected = list->currentRow() >= 0 && list->currentRow() < matches.size() ? matches[list->currentRow()] : -1;
    const QSignalBlocker guard(list);
    list->clear(); matches.clear();
    bool pending = false;
    const auto words = query->text().simplified().split(' ', Qt::SkipEmptyParts);
    for (int i = 0; i < snapshot.size(); ++i) {
        const auto& record = snapshot[i];
        QString haystack = record.commandToken + ' ' + record.label + ' ' + record.description;
        for (const auto& alias : record.command.value("aliases").toList()) haystack += ' ' + alias.toString();
        bool match = true;
        for (const auto& word : words) if (!haystack.contains(word, Qt::CaseInsensitive)) { match = false; break; }
        if (!match) continue;
        matches.append(i);
        auto* row = new QListWidgetItem(record.label, list);
        const bool block = record.command.value("kind").toString() == "block";
        row->setData(Qt::UserRole + 1, block ? "Block" : "Template");
        const auto result = eligibility(record);
        pending |= result.state == Eligibility::Pending;
        row->setData(Qt::UserRole + 20, result.state == Eligibility::Available ? "Available"
            : result.state == Eligibility::Pending ? "Waiting" : "Unavailable");
        row->setData(Qt::UserRole + 21, result.reason);
        row->setToolTip(record.description + '\n' + result.reason);
    }
    if (list->count()) list->setCurrentRow(qMax(0, matches.indexOf(selected)));
    showEligibility();
    if (pending) pendingRefresh->start(); else pendingRefresh->stop();
}

EditorSlashController::Eligibility EditorSlashController::eligibility(const UserTemplateRecord& record) const {
    if (!current()) return {Eligibility::Unavailable, "The document or insertion position changed."};
    if (editor->documentReadOnly()) return {Eligibility::Unavailable, "This document is read-only."};
    if (record.command.value("kind").toString() == "block")
        return {Eligibility::Available, "Enter to choose or insert the block · Esc cancels"};
    const auto location = record.command.value("context", "any").toString();
    if (location != "any") {
        const auto* syntax = editor->syntaxDocument(); const int position = original.selectionStart();
        if (!syntax) return {Eligibility::Unavailable, "No HDL syntax context is available."};
        if (syntax->hasPendingEdits()) return {Eligibility::Pending, "Waiting for syntax analysis. This list updates automatically."};
        if (syntax->isCommentAt(position) || syntax->isStringAt(position))
            return {Eligibility::Unavailable, "Requires code outside comments and strings."};
        if (location == "codeLine" || location == "module") {
            QTextCursor cursor(original); cursor.setPosition(position);
            if (original.hasSelection() || !cursor.block().text().trimmed().isEmpty())
                return {Eligibility::Unavailable, "Requires an empty code line. Search is available at any position."};
        }
        if (location == "module" && (!syntax->moduleScopeTarget(position).ok() || syntax->alwaysScopeTarget(position).ok()))
            return {Eligibility::Unavailable, "Requires a known module body outside a procedural block."};
    }
    const auto plan = OrdinaryTemplatePlanner::prepare(record, snapshot, original.selectedText().replace(QChar::ParagraphSeparator, '\n'));
    if (!plan.error.isEmpty()) return {Eligibility::Unavailable, plan.error};
    return {Eligibility::Available, "Enter inserts · Esc cancels"};
}
void EditorSlashController::showEligibility() {
    const auto* row = list->currentItem();
    feedback->setText(row ? row->data(Qt::UserRole + 21).toString() : "No matching resource. Edit the search or press Esc.");
}

void EditorSlashController::confirm() {
    if (preedit || compositionCommit) return;
    if (!current() || UserTemplateService::getInstance()->catalogRevision() != catalogRevision) {
        cancel(true); emit editor->editorStatusMessageRequested("The document or resource changed. Open resource search again."); return;
    }
    const int row = list->currentRow();
    if (row < 0 || row >= matches.size()) { feedback->setText("No matching resource. Nothing was inserted."); return; }
    const auto record = snapshot[matches[row]];
    const auto allowed = eligibility(record);
    if (allowed.state != Eligibility::Available) { filter(); feedback->setText(allowed.reason); return; }
    if (record.command.value("kind").toString() == "block") {
        const auto cursor = original; cancel(true);
        emit editor->editorBlockInsertRequested(record.command, cursor);
        return;
    }
    auto plan = OrdinaryTemplatePlanner::prepare(record, snapshot, original.selectedText().replace(QChar::ParagraphSeparator, '\n'));
    if (!plan.error.isEmpty()) { feedback->setText(plan.error); return; }
    const auto context = editor->editorSemanticContextForPosition(original.selectionStart());
    auto inputs = std::make_shared<TemplateInputContext>();
    inputs->snapshot = SemanticIndex::getInstance()->snapshot();
    inputs->location.fileName = context.fileName; inputs->location.moduleName = context.moduleName;
    inputs->location.packageName = context.packageName; inputs->location.cursorLine = context.cursorLine;
    inputs->location.cursorPosition = original.selectionStart();
    const QPointer<MyCodeEditor> target(editor);
    const QPointer<QTextDocument> document(source);
    const QString identity(file);
    inputs->current = [target, document, identity] { return target && document && target->document() == document && target->documentFileName() == identity; };
    inputs->liveSnapshot = [] { return SemanticIndex::getInstance()->snapshot(); };
    inputs->refreshSnapshot = true;
    for (auto& slot : plan.placeholders) slot.inputContext = inputs;
    cancel(true);
    QString error;
    editor->insertOrdinaryTemplate(plan.text, plan.placeholders, plan.finalCursor, &error);
    if (!error.isEmpty()) emit editor->editorStatusMessageRequested(error);
}

bool EditorSlashController::eventFilter(QObject* object, QEvent* event) {
    if (!searching) return false;
    if (object == popup && event->type() == QEvent::Hide) { cancel(); return false; }
    if (event->type() == QEvent::Shortcut) { cancel(true); return false; }
    if (object == editor && (event->type() == QEvent::Hide || event->type() == QEvent::Close)) { cancel(); return false; }
    // Events already addressed to the editor still belong to the active query.
    if (object == editor && (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease
        || event->type() == QEvent::ShortcutOverride || event->type() == QEvent::InputMethod)) {
        QApplication::sendEvent(query, event);
        return true;
    }
    if (object == query && event->type() == QEvent::InputMethod) {
        const auto* input = static_cast<QInputMethodEvent*>(event);
        preedit = !input->preeditString().isEmpty();
        compositionCommit = !input->commitString().isEmpty();
        QTimer::singleShot(0, this, [this] { compositionCommit = false; });
    }
    if (object != query || event->type() != QEvent::KeyPress) return false;
    auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() == Qt::Key_Escape || (key->key() == Qt::Key_Backspace && query->text().isEmpty() && !preedit)) {
        cancel(true); return true;
    }
    if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) { confirm(); return true; }
    if (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down) {
        if (list->count()) list->setCurrentRow((list->currentRow() + (key->key() == Qt::Key_Up ? -1 : 1) + list->count()) % list->count());
        return true;
    }
    if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) {
        cancel(true); QApplication::sendEvent(editor, key); return true;
    }
    if (query->text().isEmpty() && !preedit && (key->text() == " " || key->text() == "/" || key->text() == "*")) {
        if (!current()) { cancel(true); return true; }
        const QString text = "/" + key->text(); cancel(true);
        auto cursor = editor->textCursor(); cursor.beginEditBlock(); cursor.insertText(text); cursor.endEditBlock(); editor->setTextCursor(cursor);
        return true;
    }
    if (key->key() == Qt::Key_Control || key->key() == Qt::Key_Shift
        || key->key() == Qt::Key_Alt || key->key() == Qt::Key_Meta || key->key() == Qt::Key_AltGr)
        return false;
    if ((key->modifiers() & (Qt::ControlModifier | Qt::MetaModifier))
        && !key->matches(QKeySequence::Paste) && !key->matches(QKeySequence::Copy)
        && !key->matches(QKeySequence::Cut) && !key->matches(QKeySequence::SelectAll)) {
        cancel(true); QApplication::sendEvent(editor, key); return true;
    }
    return false;
}
