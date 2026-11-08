#include "editorblockhost.h"
#include "editorruntime.h"
#include "mycodeeditor.h"
#include "uicontrols.h"
#include "uidialogs.h"
#include "documentblockstore.h"
#include "editorblockpreview.h"
#include "pinloompreviewimage.h"
#include <QApplication>
#include <QClipboard>
#include <QFrame>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QKeyEvent>
#include <QTextLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollBar>
#include <QTimer>
#include <QMenu>
#include <QSet>
#include <QFileDialog>
#include <QPainter>
#include <QDesktopServices>
#include <QtMath>
#include <algorithm>

namespace {
class BlockFrame final : public QFrame {
public:
    using QFrame::QFrame;
protected:
    void paintEvent(QPaintEvent* event) override {
        QFrame::paintEvent(event);
        if (hasFocus()) {
            QPainter painter(this); painter.setPen(QPen(palette().color(QPalette::Highlight), 2));
            painter.drawRect(rect().adjusted(1,1,-2,-2));
        }
    }
};
}

EditorBlockHost::EditorBlockHost(MyCodeEditor* value) : QObject(value), editor(value), client(new PinloomHostClient(this)) {
    connect(editor, &MyCodeEditor::editorBlockInsertRequested, this, &EditorBlockHost::insert);
    connect(editor->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { schedule(); });
    connect(editor, &MyCodeEditor::fileNameChanged, this, [this] { finishEditing(); schedule(); });
    qApp->installEventFilter(this); rebind();
}
EditorBlockHost::~EditorBlockHost() {
    qApp->removeEventFilter(this);
    for (const auto& connection : connections) disconnect(connection);
    for (const auto& view : views) delete view.frame;
}
void EditorBlockHost::rebind() {
    finishEditing();
    for (const auto& connection : connections) disconnect(connection);
    connections.clear();
    for (const auto& view : views) delete view.frame;
    views.clear(); selectedId.clear(); model = DocumentBlockModel::get(editor->document());
    connections << connect(model, &DocumentBlockModel::changed, this, [this] {
        editor->invalidateViewProjection(); editor->viewport()->update(); schedule();
    });
    connections << connect(model, &DocumentBlockModel::errorChanged, editor, &MyCodeEditor::editorStatusMessageRequested);
    schedule();
}
void EditorBlockHost::finishEditing() { if (model) model->endTextSession(); }
void EditorBlockHost::schedule() {
    if (scheduled) return; scheduled = true;
    QTimer::singleShot(0, this, [this] { scheduled = false; updateVisible(); });
}
EditorBlockHost::View EditorBlockHost::createView(const EditorAssociatedBlock& block) {
    View view;
    view.frame = new BlockFrame(editor->viewport()); view.frame->setObjectName("editorBlockShell");
    view.frame->setProperty("blockId", block.id); view.frame->setFrameShape(QFrame::StyledPanel);
    view.frame->setAutoFillBackground(true); view.frame->setFocusPolicy(Qt::StrongFocus);
    auto* layout = new QVBoxLayout(view.frame); layout->setContentsMargins(6, 3, 6, 3); layout->setSpacing(3);
    auto* header = new QHBoxLayout; layout->addLayout(header);
    view.title = UiControls::label(view.frame); header->addWidget(view.title, 1);
    view.title->setTextFormat(Qt::PlainText);
    view.title->setMinimumWidth(0); view.title->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    auto* menu = new QMenu(view.frame);
    auto* actions = UiControls::pushButton("Actions", view.frame); actions->setMenu(menu); header->addWidget(actions);
    auto button = [&](const QString& text, const QString& tooltip, auto action) -> QPushButton* {
        if (text != "Collapse") {
            auto* item = menu->addAction(text); item->setToolTip(tooltip);
            item->setEnabled(!editor->documentReadOnly() || text == "Copy");
            connect(item, &QAction::triggered, this, action); return nullptr;
        }
        auto* result = UiControls::pushButton(text, view.frame); result->setToolTip(tooltip);
        result->setEnabled(!editor->documentReadOnly());
        result->setFixedHeight(26); header->addWidget(result); connect(result, &QPushButton::clicked, this, action); return result;
    };
    const auto id = block.id;
    view.collapse = button("Collapse", "Collapse or expand this block", [this,id] {
        if (const auto* current = model->find(id)) { auto next = *current; next.collapsed = !next.collapsed; model->update(next); }
    });
    button("Up", "Move before the previous block at this anchor", [this,id] { model->reorder(id, -1); });
    button("Down", "Move after the next block at this anchor", [this,id] { model->reorder(id, 1); });
    button("Attach", "Attach to the current code selection or line", [this,id] { model->reattach(id, editor->textCursor()); });
    button("Copy", "Copy this block's content", [this,id] {
        if (const auto* current = model->find(id)) QApplication::clipboard()->setText(current->type == "text" ? current->text : current->uri);
    });
    button("Delete", "Remove this block (undoable)", [this,id] { model->remove(id); editor->setFocus(); });
    button("Height", "Set display height", [this,id] {
        const auto* current = model->find(id); if (!current) return; const auto revision = model->revision();
        bool accepted = false; const int height = UiDialogs::getInt(editor, "Block height", "Height in logical pixels", current->height, 60, 600, 10, &accepted);
        if (accepted && model->revision() == revision && model->find(id)) { auto next = *model->find(id); next.height = height; model->update(next); }
    });
    if (block.type != "text") {
        button("Caption", "Edit the preview caption", [this,id] {
            const auto* current = model->find(id); if (!current) return; const auto revision = model->revision();
            bool accepted = false; const auto text = UiDialogs::getText(editor, "Preview caption", "Caption", QLineEdit::Normal, current->caption, &accepted);
            if (accepted && model->revision() == revision && model->find(id)) { auto next = *model->find(id); next.caption = text; model->update(next); }
        });
        button("Width", "Set maximum image width", [this,id] {
            const auto* current = model->find(id); if (!current) return; const auto revision = model->revision();
            bool accepted = false; const int width = UiDialogs::getInt(editor, "Preview width", "Maximum width in logical pixels", current->width, 120, 1600, 10, &accepted);
            if (accepted && model->revision() == revision && model->find(id)) { auto next = *model->find(id); next.width = width; model->update(next); }
        });
        auto* open = menu->addAction("Open original");
        connect(open, &QAction::triggered, this, [this,id] { openOriginal(id); });
    }
    if (block.type == "text") {
        view.text = new QPlainTextEdit(view.frame); view.text->setObjectName("editorBlockText");
        view.text->setPlainText(block.text); view.text->setReadOnly(editor->documentReadOnly());
        layout->addWidget(view.text, 1);
        connect(view.text, &QPlainTextEdit::textChanged, this, [this,id,text=view.text] { if (!updating) model->editText(id, text->toPlainText()); });
    } else {
        view.resource = new EditorBlockPreview(model, client, view.frame);
        view.image = new PinloomPreviewImageLabel(view.frame); view.image->hide();
        auto* imageRow = new QHBoxLayout;
        imageRow->addStretch(); imageRow->addWidget(view.image, 1); imageRow->addStretch();
        layout->addLayout(imageRow, 1);
        view.image->activated = [this,id] { openOriginal(id); };
        view.preview = UiControls::label(view.frame); view.preview->setWordWrap(true);
        view.preview->setTextFormat(Qt::PlainText); view.preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
        view.preview->setAlignment(Qt::AlignLeft | Qt::AlignTop); layout->addWidget(view.preview);
    }
    return view;
}
void EditorBlockHost::updateVisible() {
    if (updating || !model) return;
    if (!editor->isVisible()) {
        finishEditing(); selectedId.clear();
        for (const auto& view : views) delete view.frame;
        views.clear(); return;
    }
    updating = true;
    editor->state->projection.ensure(editor, editor->state->folding.collapsedLineRanges());
    const auto demanded = editor->state->projection.visibleAssociatedBlocks(editor, editor->viewport()->height());
    QSet<QString> keep;
    for (const auto& item : demanded) {
        const auto* block = model->find(item.first); if (!block) continue;
        keep.insert(item.first);
        if (!views.contains(item.first)) views.insert(item.first, createView(*block));
        auto& view = views[item.first];
        view.frame->setGeometry(item.second.toAlignedRect());
        if (view.revision != block->revision) {
            view.title->setText(QString(block->missing ? "Missing anchor · " : "") + (block->type == "text" ? "Text note" : block->type == "image" ? "Image" : "Reference"));
            view.collapse->setText(block->collapsed ? "Expand" : "Collapse");
            if (view.text && view.text->toPlainText() != block->text) view.text->setPlainText(block->text);
            if (view.image) view.image->setMaximumWidth(block->width);
            view.revision = block->revision;
        }
        const bool compact = item.second.height() <= 36;
        if (view.text) view.text->setVisible(!compact);
        if (view.preview) view.preview->setVisible(!compact);
        view.frame->show();
        const auto resourceKey = model->documentId() + '\n' + block->asset + '\n' + block->uri + '\n' + block->caption;
        if (view.resource && compact) {
            view.resource->cancel(); view.resourceKey.clear(); view.image->clearSourcePixmap(); view.image->hide();
        } else if (view.resource && view.resourceKey != resourceKey) {
            view.resourceKey = resourceKey;
            const QPointer<QFrame> frame = view.frame; const auto id = block->id;
            view.resource->request(id, [this,frame,id](const EditorBlockPreviewResult& result) {
                if (!frame || !views.contains(id) || views[id].frame != frame || !model->find(id)) return;
                auto& visible = views[id]; const auto* current = model->find(id);
                QStringList text; if (!current->caption.isEmpty()) text << current->caption;
                if (!result.status.isEmpty()) text << result.status;
                if (!result.text.isEmpty()) text << result.text;
                visible.preview->setText(text.join('\n')); visible.preview->setVisible(!text.isEmpty());
                if (!result.image.isNull()) visible.image->setSourcePixmap(QPixmap::fromImage(result.image), current->caption);
                else visible.image->clearSourcePixmap();
                visible.image->setVisible(!result.image.isNull());
            });
        }
    }
    for (auto it = views.begin(); it != views.end();) {
        const bool focused = it->frame == QApplication::focusWidget() || it->frame->isAncestorOf(QApplication::focusWidget());
        if ((!keep.contains(it.key()) && !focused) || !model->find(it.key())) {
            if (focused) editor->setFocus();
            auto* frame = it->frame; it = views.erase(it); frame->deleteLater();
        } else ++it;
    }
    updating = false;
    editor->state->gutter.handleUpdateRequest(editor, editor->viewport()->rect(), 0);
}
void EditorBlockHost::insert(const QVariantMap& preset, const QTextCursor& cursor) {
    if (editor->documentReadOnly() || cursor.document() != editor->document()) return;
    const auto type = preset.value("blockType").toString();
    const QPointer<QTextDocument> document = editor->document(); const auto revision = editor->semanticDocumentRevision(); const auto file = editor->documentFileName();
    const auto current = [&] { return document && editor->document() == document && editor->semanticDocumentRevision() == revision
        && editor->documentFileName() == file && !editor->documentReadOnly(); };
    EditorAssociatedBlock block; block.type = type; block.text = preset.value("text").toString();
    block.caption = preset.value("caption").toString(); block.width = qBound(120, preset.value("width", 480).toInt(), 1600); block.height = qBound(60, preset.value("height", 160).toInt(), 600);
    if (type == "image") {
        const auto path = QFileDialog::getOpenFileName(editor, "Import image", {}, "Images (*.png *.jpg *.jpeg *.webp *.bmp *.gif)");
        if (path.isEmpty() || !current()) return;
        QString issue; block.asset = DocumentBlockStore::get(model)->importImage(path, &issue);
        if (block.asset.isEmpty()) { emit editor->editorStatusMessageRequested(issue); return; }
    } else if (type == "reference") {
        bool accepted = false;
        block.uri = UiDialogs::getText(editor, "Reference preview", "Existing object URI", QLineEdit::Normal, preset.value("uri").toString(), &accepted).trimmed();
        if (!accepted || !current()) return;
        const QUrl uri(block.uri, QUrl::StrictMode);
        if (!uri.isValid() || uri.isRelative() || block.uri.isEmpty()) { emit editor->editorStatusMessageRequested("Enter a valid existing object URI."); return; }
    }
    if (!current()) return;
    const auto id = model->insert(block, cursor);
    select(id, type == "text");
}
void EditorBlockHost::select(const QString& id, bool edit) {
    const auto* block = model->find(id); if (!block) return;
    selectedId = id; editor->state->projection.ensure(editor, editor->state->folding.collapsedLineRanges());
    const auto rect = editor->state->projection.associatedBlockRect(editor, id);
    if (rect.top() < 0) editor->verticalScrollBar()->setValue(editor->verticalScrollBar()->value() + qFloor(rect.top()));
    else if (rect.bottom() > editor->viewport()->height()) editor->verticalScrollBar()->setValue(editor->verticalScrollBar()->value() + qCeil(rect.bottom() - editor->viewport()->height()));
    updateVisible();
    const auto view = views.value(id);
    if (view.frame) {
        if (edit && view.text && view.text->isVisible()) { model->beginTextSession(id); view.text->setFocus(); }
        else view.frame->setFocus();
    }
}
void EditorBlockHost::returnToCode(const EditorAssociatedBlock& block) {
    finishEditing(); selectedId.clear();
    QTextCursor cursor(editor->document()); const int end = editor->document()->characterCount() - 1;
    cursor.setPosition(qBound(0, block.returnAnchor, end)); cursor.setPosition(qBound(0, block.returnPosition, end), QTextCursor::KeepAnchor);
    editor->setTextCursor(cursor); editor->setFocus(); editor->ensureCursorVisible();
}
void EditorBlockHost::leaveBlock(const QString& id, bool forward) {
    const auto* current = model->find(id); if (!current) return;
    editor->state->projection.ensure(editor, editor->state->folding.collapsedLineRanges());
    const auto next = editor->state->projection.neighboringAssociatedBlock(id, forward);
    if (!next.isEmpty()) { select(next); return; }
    const auto position = editor->document()->findBlockByNumber(editor->state->projection.sourceLineForAssociatedBlock(id));
    if (!position.isValid()) { returnToCode(*current); return; }
    auto target = forward ? editor->nextVisibleBlock(position) : position;
    if (!target.isValid()) target = position;
    QTextCursor cursor(target); if (!forward) cursor.movePosition(QTextCursor::EndOfBlock);
    selectedId.clear(); finishEditing(); editor->setTextCursor(cursor); editor->setFocus(); editor->ensureCursorVisible();
}
bool EditorBlockHost::navigateFromCode(QKeyEvent* event) {
    if (!model || model->items().isEmpty() || editor->textCursor().hasSelection() || event->modifiers() != Qt::NoModifier
        || (event->key() != Qt::Key_Up && event->key() != Qt::Key_Down)) return false;
    const auto cursor = editor->textCursor(); auto* layout = cursor.block().layout();
    const auto visual = layout ? layout->lineForTextPosition(cursor.positionInBlock()) : QTextLine();
    const bool forward = event->key() == Qt::Key_Down;
    if (visual.isValid() && ((forward && visual.lineNumber() + 1 < layout->lineCount()) || (!forward && visual.lineNumber() > 0))) return false;
    editor->state->projection.ensure(editor, editor->state->folding.collapsedLineRanges());
    const auto id = editor->state->projection.adjacentAssociatedBlock(cursor.blockNumber(), forward);
    if (id.isEmpty()) return false; select(id); return true;
}
bool EditorBlockHost::eventFilter(QObject* object, QEvent* event) {
    if (!model) return false;
    if (object == editor && (event->type() == QEvent::Hide || event->type() == QEvent::Show)) schedule();
    if (object == editor && event->type() == QEvent::FocusIn) { selectedId.clear(); finishEditing(); }
    if (event->type() == QEvent::Resize && object == editor->viewport()) schedule();
    auto* widget = qobject_cast<QWidget*>(object); if (!widget) return false;
    QString id;
    for (auto it = views.cbegin(); it != views.cend(); ++it) if (widget == it->frame || it->frame->isAncestorOf(widget)) { id = it.key(); break; }
    if (id.isEmpty()) return false;
    const auto* block = model->find(id); if (!block) return false;
    const auto view = views.value(id);
    if (event->type() == QEvent::FocusIn) { selectedId = id; view.frame->update(); if (widget == view.text) model->beginTextSession(id); }
    if (event->type() == QEvent::FocusOut) view.frame->update();
    if (event->type() == QEvent::FocusOut && widget == view.text) finishEditing();
    if (event->type() != QEvent::KeyPress) return false;
    auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() == Qt::Key_Escape) { if (widget == view.frame) returnToCode(*block); else { finishEditing(); view.frame->setFocus(); } return true; }
    if (key->matches(QKeySequence::Undo) || key->matches(QKeySequence::Redo)) { finishEditing(); if (key->matches(QKeySequence::Undo)) editor->undo(); else editor->redo(); return true; }
    if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) {
        QList<QWidget*> controls;
        for (auto* control : view.frame->findChildren<QWidget*>()) if (control->isVisible() && control->isEnabled() && (control->focusPolicy() & Qt::TabFocus)) controls.append(control);
        if (!controls.isEmpty()) { int index = controls.indexOf(widget); const bool back = key->key() == Qt::Key_Backtab || key->modifiers().testFlag(Qt::ShiftModifier); index = (index + (back ? -1 : 1) + controls.size()) % controls.size(); controls[index]->setFocus(); }
        return true;
    }
    if (widget != view.frame) return false;
    if (key->key() == Qt::Key_Delete && !editor->documentReadOnly()) { model->remove(id); editor->setFocus(); return true; }
    if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) { if (block->type == "text") select(id, true); else openOriginal(id); return true; }
    if (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down) { leaveBlock(id, key->key() == Qt::Key_Down); return true; }
    return false;
}

void EditorBlockHost::openOriginal(const QString& id) {
    const auto* block = model->find(id); if (!block) return;
    if (block->type == "image") {
        const auto view = views.value(id);
        if (view.image && !view.image->sourcePixmap().isNull()) openPinloomImagePreview(editor, view.image->sourcePixmap(), "Image", block->caption);
        else emit editor->editorStatusMessageRequested("The image is unavailable or still loading.");
        return;
    }
    const QUrl uri(block->uri, QUrl::StrictMode); const auto identity = PinloomHostIdentity::fromUri(uri);
    if (identity.isValid()) {
        const QPointer<DocumentBlockModel> bound = model; const QPointer<EditorBlockHost> self(this);
        client->open(identity, [self,bound,id](bool opened, const QString& error) { if (self && bound && bound == self->model && bound->find(id) && !opened) emit self->editor->editorStatusMessageRequested(error); }, true);
    } else if ((uri.isLocalFile() || uri.scheme() == "https" || uri.scheme() == "http") && QDesktopServices::openUrl(uri)) return;
    else emit editor->editorStatusMessageRequested("This URI has no supported open action.");
}
void EditorBlockHost::addAssociationActions(QMenu* menu) {
    auto* actions = menu->addMenu("Block associations");
    auto* attach = actions->addAction("Attach associations…"); attach->setEnabled(!editor->documentReadOnly());
    connect(attach, &QAction::triggered, this, [this] {
        const QPointer<DocumentBlockModel> bound = model; const auto revision = model->revision();
        const auto path = QFileDialog::getOpenFileName(editor, "Attach associations", {}, "Association data (*.json)");
        if (path.isEmpty() || bound != model || model->revision() != revision) return;
        QString issue; DocumentBlockStore::get(model)->attach(path, &issue); if (!issue.isEmpty()) emit editor->editorStatusMessageRequested(issue);
    });
    auto* store = DocumentBlockStore::get(model);
    if (store->blocked()) {
        auto* resolve = actions->addAction("Use matching saved generation");
        connect(resolve, &QAction::triggered, this, [this] { QString issue; if (!DocumentBlockStore::get(model)->acceptMatchingGeneration(&issue)) emit editor->editorStatusMessageRequested(issue); });
    }
}
