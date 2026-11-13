#include "editorblockhost.h"
#include "editorruntime.h"
#include "mycodeeditor.h"
#include "uicontrols.h"
#include "uidialogs.h"
#include "documentblockstore.h"
#include "editorblockpreview.h"
#include "editorblockcontent.h"
#include "waveblockcontent.h"
#include "blocknotestyle.h"
#include "insightvisualstyle.h"
#include "uitypography.h"
#include "roundedicons.h"
#include "pinloompreviewimage.h"
#include "pinloomreferencepicker.h"
#include <QApplication>
#include <QClipboard>
#include <QFrame>
#include <QLabel>
#include <QTextEdit>
#include <QToolButton>
#include <QMouseEvent>
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
#include <QFileInfo>
#include <utility>
#include <QPainter>
#include <QtMath>
#include <algorithm>

namespace {
class BlockFrame final : public QFrame {
public:
    using QFrame::QFrame;
    QWidget* tools = nullptr;
protected:
    void resizeEvent(QResizeEvent* event) override {
        QFrame::resizeEvent(event);
        if (tools) { tools->adjustSize(); tools->move(qMax(4, width()-tools->width()-4), 4); tools->raise(); }
    }
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
    connect(&BlockNoteStyle::instance(), &BlockNoteStyle::changed, this, [this] {
        editor->invalidateViewProjection(); editor->viewport()->update(); schedule();
    });
    connect(editor, &MyCodeEditor::editorBlockInsertRequested, this, &EditorBlockHost::insert);
    connect(editor->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { schedule(); });
    connect(editor, &MyCodeEditor::fileNameChanged, this, [this] { finishEditing(); schedule(); });
    qApp->installEventFilter(this); rebind();
}
EditorBlockHost::~EditorBlockHost() {
    qApp->removeEventFilter(this);
    for (const auto& connection : connections) disconnect(connection);
    const auto oldViews=std::exchange(views,{});
    for (const auto& view : oldViews) delete view.frame;
}
void EditorBlockHost::rebind() {
    finishEditing();
    client->cancelReadRequests(this);
    for (const auto& connection : connections) disconnect(connection);
    connections.clear();
    const auto oldViews=std::exchange(views,{});
    for (const auto& view : oldViews) delete view.frame;
    selectedId.clear(); model = DocumentBlockModel::get(editor->document());
    connections << connect(model, &DocumentBlockModel::changed, this, [this] {
        editor->invalidateViewProjection(); editor->viewport()->update(); schedule();
    });
    connections << connect(model, &DocumentBlockModel::errorChanged, editor, &MyCodeEditor::editorStatusMessageRequested);
    schedule();
}
void EditorBlockHost::finishEditing() {
    if (model) model->endTextSession();
    for (auto& view:views) if (view.content) view.content->setEditing(false);
}
void EditorBlockHost::schedule() {
    if (scheduled) return; scheduled = true;
    QTimer::singleShot(0, this, [this] { scheduled = false; updateVisible(); });
}
EditorBlockHost::View EditorBlockHost::createView(const EditorAssociatedBlock& block) {
    View view;
    auto* frame = new BlockFrame(editor->viewport()); view.frame = frame; view.frame->setObjectName("editorBlockShell");
    view.frame->setProperty("blockId", block.id); view.frame->setFrameShape(QFrame::NoFrame);
    view.frame->setAutoFillBackground(true); view.frame->setFocusPolicy(Qt::StrongFocus);
    view.frame->setAttribute(Qt::WA_NoMousePropagation);
    view.frame->setMouseTracking(true);
    auto* layout = new QVBoxLayout(view.frame); layout->setContentsMargins(12, 10, 12, 10); layout->setSpacing(4);
    layout->setSizeConstraint(QLayout::SetNoConstraint);
    view.title = UiControls::label(view.frame); layout->addWidget(view.title);
    UiTypography::apply(view.title, UiTypography::Role::Metadata);
    view.title->setTextFormat(Qt::PlainText);
    view.title->setMinimumWidth(0); view.title->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    view.tools = new QWidget(view.frame); view.tools->setObjectName("editorBlockTools");
    frame->tools = view.tools;
    auto* header = new QHBoxLayout(view.tools); header->setContentsMargins(2,2,2,2); header->setSpacing(2);
    const auto tool = [&](RoundedIcons::Kind icon, const QString& label) {
        auto* button = UiControls::toolButton(view.tools); button->setIcon(RoundedIcons::icon(icon));
        button->setIconSize(QSize(16,16)); button->setFixedSize(26,26); button->setAutoRaise(true);
        button->setToolTip(label); button->setAccessibleName(label); header->addWidget(button); return button;
    };
    auto* grip = tool(RoundedIcons::Grip, "Drag block"); grip->setObjectName("editorBlockDragHandle");
    grip->setCursor(Qt::OpenHandCursor); grip->setEnabled(!editor->documentReadOnly());
    auto* menu = new QMenu(view.frame);
    auto* actions = tool(RoundedIcons::More, "Block actions"); actions->setMenu(menu); actions->setPopupMode(QToolButton::InstantPopup);
    auto button = [&](const QString& text, const QString& tooltip, auto action) -> QToolButton* {
        if (text != "Collapse") {
            auto* item = menu->addAction(text); item->setToolTip(tooltip);
            item->setEnabled(!editor->documentReadOnly() || text == "Copy");
            connect(item, &QAction::triggered, this, [this,text,action] { if (text == "Copy" || !editor->documentReadOnly()) action(); }); return nullptr;
        }
        auto* result = tool(RoundedIcons::Collapse, tooltip);
        result->setEnabled(!editor->documentReadOnly());
        connect(result, &QToolButton::clicked, this, [this,action] { if (!editor->documentReadOnly()) action(); }); return result;
    };
    const auto id = block.id;
    view.collapse = button("Collapse", "Collapse or expand this block", [this,id] {
        if (const auto* current = model->find(id)) { auto next = *current; next.collapsed = !next.collapsed; model->update(next); }
    });
    button("Attach", "Attach to the current code selection or line", [this,id] { model->reattach(id, editor->textCursor()); });
    button("Copy", "Copy block", [this,id] { emit editor->editorBlockClipboardRequested(id,false); });
    button("Cut", "Move this block when pasted", [this,id] { emit editor->editorBlockClipboardRequested(id,true); });
    button("Delete", "Remove this block (undoable)", [this,id] { model->remove(id); editor->setFocus(); });
    auto* remove = tool(RoundedIcons::Close, "Delete block"); remove->setEnabled(!editor->documentReadOnly());
    connect(remove, &QToolButton::clicked, this, [this,id] { if (!editor->documentReadOnly()) { model->remove(id); editor->setFocus(); } });
    EditorBlockContent::addAppearanceMenu(menu,model,id,view.frame,[this] { return editor->documentReadOnly(); });
    if (block.type == "text" || block.type == "code" || !EditorBlockPreview::linkOnlyText(block).isEmpty()) button("Auto height", "Fit the text in this view", [this,id] {
        if (const auto* current = model->find(id)) { auto next = *current; next.autoHeight = true; model->update(next); }
    });
    button("Height", "Set display height", [this,id] {
        const auto* current = model->find(id); if (!current) return; const auto revision = model->revision();
        bool accepted = false; const int height = UiDialogs::getInt(editor, "Block height", "Height in logical pixels", current->height, 60, 600, 10, &accepted);
        if (accepted && !editor->documentReadOnly() && model->revision() == revision && model->find(id)) { auto next = *model->find(id); next.height = height; next.autoHeight = false; model->update(next); }
    });
    if (block.type == "image" || block.type=="reference") {
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
        if (block.type == "reference") EditorBlockPreview::bindOriginalAction(open,menu,model,client,id);
        connect(open, &QAction::triggered, this, [this,id] { openOriginal(id); });
    }
    view.content = new EditorBlockContent(model, client, block, view.frame);
    view.content->openOriginal = [this,id] { openOriginal(id); };
    view.text = view.content->textEditor(); layout->addWidget(view.content, 1);
    view.tools->hide();
    return view;
}
void EditorBlockHost::updateVisible() {
    if (updating || !model) return;
    if (!editor->isVisible()) {
        finishEditing(); selectedId.clear();
        const auto oldViews=std::exchange(views,{});
        for (const auto& view : oldViews) delete view.frame;
        return;
    }
    updating = true;
    editor->state->projection.ensure(editor, editor->state->folding.collapsedLineRanges());
    editor->state->projection.prepareVisibleGeometry(editor);
    const auto demanded = editor->state->projection.visibleAssociatedBlocks(editor, editor->viewport()->height());
    QSet<QString> keep;
    for (const auto& item : demanded) {
        const auto* block = model->find(item.first); if (!block) continue;
        keep.insert(item.first);
        if (!views.contains(item.first)) views.insert(item.first, createView(*block));
        auto& view = views[item.first];
        if (view.revision != block->revision || view.styleRevision != BlockNoteStyle::instance().revision()) {
            const auto label=block->type=="text" ? "Text note" : block->type=="image" ? "Image" : block->type=="code" ? "Code" : block->type=="wave" ? "Wave" : block->type=="attachment" ? "Attachment" : "Reference";
            view.title->setText(QString(block->missing ? "Missing anchor · " : "") + label);
            view.collapse->setIcon(RoundedIcons::icon(block->collapsed ? RoundedIcons::Expand : RoundedIcons::Collapse));
            view.collapse->setToolTip(block->collapsed ? "Expand" : "Collapse");
            const auto style = BlockNoteStyle::instance().appearance(block->appearance);
            view.frame->setStyleSheet(QStringLiteral("QFrame#editorBlockShell { background:%1; border:1px solid %2; border-radius:6px; }"
                "QWidget#editorBlockTools { background:%3; border-radius:4px; }"
                "QTextEdit#editorBlockText { background:transparent; color:%4; border:0; }")
                .arg(style.background.name(QColor::HexArgb), style.border.name(QColor::HexArgb),
                     InsightVisualStyle::theme().surface.raised.name(), style.text.name()));
            view.revision = block->revision;
            view.styleRevision = BlockNoteStyle::instance().revision();
        }
        const bool compact = item.second.height() <= 36;
        view.title->setVisible(compact || block->missing);
        view.content->present(*block, editor->documentReadOnly(), compact);
        view.frame->setGeometry(item.second.toAlignedRect());
        view.frame->show();
    }
    for (auto it = views.begin(); it != views.end();) {
        const bool focused = it->frame == QApplication::focusWidget() || it->frame->isAncestorOf(QApplication::focusWidget());
        if ((!keep.contains(it.key()) && !focused) || !model->find(it.key()) || model->find(it.key())->detached) {
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
    const QPointer<EditorBlockHost> self(this);
    const QPointer<MyCodeEditor> target(editor);
    const QPointer<DocumentBlockModel> bound(model); const auto associationRevision = model->revision();
    const auto current = [self,target,document,revision,file,bound,associationRevision,cursor] {
        return self && target && document && bound && target->document() == document && target->semanticDocumentRevision() == revision
            && target->documentFileName() == file && !target->documentReadOnly() && target->textCursor() == cursor
            && bound->revision() == associationRevision;
    };
    EditorAssociatedBlock block; block.type = type; block.text = preset.value("text").toString();
    block.caption = preset.value("caption").toString(); block.width = qBound(120, preset.value("width", 480).toInt(), 1600); block.height = qBound(60, preset.value("height", 160).toInt(), 600);
    block.autoHeight = (type == "text" || type=="code") && !preset.contains("height");
    if (type == "image") {
        const auto path = QFileDialog::getOpenFileName(editor, "Import image", {}, "Images (*.png *.jpg *.jpeg *.webp *.bmp *.gif)");
        if (path.isEmpty() || !current()) return;
        QString issue; block.asset = DocumentBlockStore::get(model)->importImage(path, &issue);
        if (block.asset.isEmpty()) { emit editor->editorStatusMessageRequested(issue); return; }
    } else if (type == "attachment") {
        const auto path=QFileDialog::getOpenFileName(editor,"Attach file");
        if (path.isEmpty() || !current()) return;
        QString issue; block.asset=DocumentBlockStore::get(model)->importFile(path,&issue); block.caption=QFileInfo(path).fileName();
        if(block.asset.isEmpty()) { emit editor->editorStatusMessageRequested(issue); return; }
    } else if (type == "wave") {
        if(block.text.isEmpty()) block.text=WaveBlockContent::initialProject();
        QString issue; if(!WaveBlockContent::validate(block.text.toUtf8(),&issue)) { emit editor->editorStatusMessageRequested(issue); return; }
        block.height=360;
    } else if (type == "reference") {
        PinloomReferencePicker picker(client, preset.value("uri").toString(), editor);
        if (picker.exec() != QDialog::Accepted || !current()) return;
        block.uri = picker.selectedUri();
        if (block.caption.isEmpty()) block.caption = picker.selectedCaption();
        if (block.uri.isEmpty()) return;
        block.autoHeight = !preset.contains("height") && !EditorBlockPreview::linkOnlyText(block).isEmpty();
    }
    if (!current()) return;
    const auto id = model->insert(block, cursor);
    select(id);
}
void EditorBlockHost::select(const QString& id, bool edit) {
    const auto* block = model->find(id); if (!block) return;
    finishEditing();
    selectedId = id; editor->state->projection.ensure(editor, editor->state->folding.collapsedLineRanges());
    editor->state->projection.prepareVisibleGeometry(editor);
    const auto rect = editor->state->projection.associatedBlockRect(editor, id);
    if (rect.top() < 0) editor->verticalScrollBar()->setValue(editor->verticalScrollBar()->value() + qFloor(rect.top()));
    else if (rect.bottom() > editor->viewport()->height()) editor->verticalScrollBar()->setValue(editor->verticalScrollBar()->value() + qCeil(rect.bottom() - editor->viewport()->height()));
    updateVisible();
    const auto view = views.value(id);
    if (view.frame) {
        if (edit && view.content && view.content->isVisible()) view.content->setEditing(true);
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
    if ((event->type()==QEvent::MouseButtonPress || event->type()==QEvent::MouseButtonDblClick)
        && widget==view.frame && static_cast<QMouseEvent*>(event)->button()==Qt::LeftButton) {
        select(id); return false;
    }
    if (event->type() == QEvent::FocusIn) { selectedId = id; view.frame->update(); if (widget == view.text) model->beginTextSession(id); }
    if (event->type() == QEvent::MouseMove || event->type() == QEvent::Enter || event->type() == QEvent::Leave
        || event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut) {
        const QPointer<QWidget> frame = view.frame, tools = view.tools;
        QTimer::singleShot(0, this, [frame,tools] {
            EditorBlockContent::refreshTools(frame,tools);
        });
    }
    if (event->type() == QEvent::FocusOut) view.frame->update();
    if (event->type() == QEvent::FocusOut && widget == view.text) finishEditing();
    if (event->type() != QEvent::KeyPress) return false;
    auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() == Qt::Key_Escape) { if (widget == view.frame) returnToCode(*block); else { finishEditing(); view.frame->setFocus(); } return true; }
    if (key->matches(QKeySequence::Undo) || key->matches(QKeySequence::Redo)) { if(view.content->nativeWaveEditing()) return false; finishEditing(); if (key->matches(QKeySequence::Undo)) editor->undo(); else editor->redo(); return true; }
    if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) {
        if (view.content->isEditing()) return false;
        view.tools->show();
        QList<QWidget*> controls;
        for (auto* control : view.tools->findChildren<QWidget*>()) if (control->isVisible() && control->isEnabled() && (control->focusPolicy() & Qt::TabFocus)) controls.append(control);
        if (!controls.isEmpty()) { int index = controls.indexOf(widget); const bool back = key->key() == Qt::Key_Backtab || key->modifiers().testFlag(Qt::ShiftModifier); index = (index + (back ? -1 : 1) + controls.size()) % controls.size(); controls[index]->setFocus(); }
        return true;
    }
    if (widget != view.frame) return false;
    if (key->key() == Qt::Key_Delete && !editor->documentReadOnly()) { model->remove(id); editor->setFocus(); return true; }
    if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) { select(id, true); return true; }
    if (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down) { leaveBlock(id, key->key() == Qt::Key_Down); return true; }
    return false;
}

void EditorBlockHost::openOriginal(const QString& id) {
    const auto* block = model->find(id); if (!block) return;
    if (block->type == "image") {
        const auto view = views.value(id);
        auto* image=view.content ? view.content->imagePreview() : nullptr;
        if (image && !image->sourcePixmap().isNull()) openPinloomImagePreview(editor, image->sourcePixmap(), "Image", block->caption);
        else emit editor->editorStatusMessageRequested("The image is unavailable or still loading.");
        return;
    }
    const QPointer<DocumentBlockModel> bound = model; const QPointer<EditorBlockHost> self(this);
    EditorBlockPreview::openReference(model, client, id, this, [self,bound,id](bool opened, const QString& error) {
        if (self && bound && bound == self->model && bound->find(id) && !opened) emit self->editor->editorStatusMessageRequested(error);
    });
}
void EditorBlockHost::addAssociationActions(QMenu* menu) {
    auto* actions = menu->addMenu("Block associations");
    auto* pages = actions->addMenu("Blocks"); int count=0;
    for (const auto& block : model->items()) if (block.detached) {
        const auto label=!block.caption.isEmpty() ? block.caption : block.type=="text" ? block.text.section('\n',0,0).left(64) : block.type;
        auto* action=pages->addAction(label.isEmpty() ? "Note" : label); ++count;
        connect(action,&QAction::triggered,this,[this,id=block.id] { emit editor->editorBlockPageRequested(id); });
    }
    pages->setEnabled(count>0);
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
