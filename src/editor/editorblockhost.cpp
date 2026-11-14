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
#include "blockshellmetrics.h"
#include "documentreferenceservice.h"
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
    QWidget* edit = nullptr;
protected:
    void resizeEvent(QResizeEvent* event) override {
        QFrame::resizeEvent(event);
        if(edit){edit->move(4,4);edit->raise();}
        if (tools) { tools->adjustSize(); tools->move(qMax(4, width()-tools->width()-4), 4); tools->raise(); }
    }
    void paintEvent(QPaintEvent* event) override {
        QFrame::paintEvent(event);
        if (property("blockSelected").toBool()) {
            QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(palette().color(QPalette::Highlight), 1));
            painter.drawRoundedRect(rect().adjusted(1,1,-2,-2),5,5);
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
    for (const auto& view : oldViews) { delete view.grip; delete view.frame; }
}
void EditorBlockHost::rebind() {
    finishEditing();
    client->cancelReadRequests(this);
    for (const auto& connection : connections) disconnect(connection);
    connections.clear();
    const auto oldViews=std::exchange(views,{});
    for (const auto& view : oldViews) { delete view.grip; delete view.frame; }
    selectedId.clear(); model = DocumentBlockModel::get(editor->document());
    connections << connect(model, &DocumentBlockModel::changed, this, [this] {
        editor->invalidateViewProjection(); editor->viewport()->update(); schedule();
    });
    connections << connect(model, &DocumentBlockModel::presentationChanged, this, [this] {
        editor->invalidateViewProjection();editor->viewport()->update();schedule();
    });
    connections << connect(model, &DocumentBlockModel::errorChanged, editor, &MyCodeEditor::editorStatusMessageRequested);
    schedule();
}
bool EditorBlockHost::finishEditing() {
    for(auto& view:views)if(view.content && !view.content->setEditing(false))return false;
    if(model)model->endTextSession();refreshControls();return true;
}
void EditorBlockHost::refreshControls() {
    for(auto it=views.begin();it!=views.end();++it) {
        auto& view=it.value();if(!view.frame)continue;
        view.frame->setProperty("blockSelected",it.key()==selectedId);view.frame->update();
        if(view.edit){view.edit->setChecked(view.content && view.content->isEditing());view.edit->setEnabled(!editor->documentReadOnly());}
        view.frame->setProperty("blockGutterFocused",view.grip && view.grip->hasFocus());
        EditorBlockContent::refreshTools(view.frame,view.tools);
        if(view.grip) {
            const auto pointer=editor->mapFromGlobal(QCursor::pos());
            const int top=editor->mapFromGlobal(view.frame->mapToGlobal(QPoint())).y();
            const auto area=QRect(0,top,editor->width(),view.frame->height());
            view.grip->setVisible(view.frame->isVisible() && (area.contains(pointer) || view.grip->hasFocus() || it.key()==selectedId));
            view.grip->setEnabled(!editor->documentReadOnly());view.grip->raise();
        }
    }
}
EditorBlockContent* EditorBlockHost::takeContent(const QString& id) {
    auto found=views.find(id);if(found==views.end() || !found->content)return nullptr;
    auto* result=found->content.data();found->frame->layout()->removeWidget(result);
    found->content=nullptr;found->text=nullptr;result->openOriginal={};return result;
}
void EditorBlockHost::adoptContent(const QString& id,EditorBlockContent* content) {
    const auto* block=model?model->find(id):nullptr;if(!block || !content)return;
    if(!views.contains(id))views.insert(id,createView(*block,content));
    auto& view=views[id];if(view.content!=content)delete view.content.data();view.content=content;view.text=content->textEditor();
    content->setParent(view.frame);view.frame->layout()->addWidget(content);content->openOriginal=[this,id]{openOriginal(id);};
    schedule();
}
void EditorBlockHost::schedule() {
    if (scheduled) return; scheduled = true;
    QTimer::singleShot(0, this, [this] { scheduled = false; updateVisible(); });
}
EditorBlockHost::View EditorBlockHost::createView(const EditorAssociatedBlock& block,EditorBlockContent* retained) {
    View view;
    auto* frame = new BlockFrame(editor->viewport()); view.frame = frame; view.frame->setObjectName("editorBlockShell");
    view.frame->setProperty("blockId", block.id); view.frame->setFrameShape(QFrame::NoFrame);
    view.frame->setAutoFillBackground(true); view.frame->setFocusPolicy(Qt::StrongFocus);
    view.frame->setAttribute(Qt::WA_NoMousePropagation);
    view.frame->setMouseTracking(true);
    auto* layout = new QVBoxLayout(view.frame); layout->setContentsMargins(BlockShellMetrics::contents); layout->setSpacing(4);
    layout->setSizeConstraint(QLayout::SetNoConstraint);
    view.title = UiControls::label(view.frame); layout->addWidget(view.title);
    UiTypography::apply(view.title, UiTypography::Role::Metadata);
    view.title->setTextFormat(Qt::PlainText);
    connect(view.title,&QLabel::linkActivated,this,[this,id=block.id]{openOriginal(id);});
    view.title->setMinimumWidth(0); view.title->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    view.tools = new QWidget(view.frame); view.tools->setObjectName("editorBlockTools");
    frame->tools = view.tools;
    auto* header = new QHBoxLayout(view.tools); header->setContentsMargins(2,2,2,2); header->setSpacing(2);
    const auto tool = [&](RoundedIcons::Kind icon, const QString& label) {
        auto* button = UiControls::toolButton(view.tools); button->setIcon(RoundedIcons::icon(icon));
        button->setIconSize(QSize(16,16)); button->setFixedSize(26,26); button->setAutoRaise(true);
        button->setToolTip(label); button->setAccessibleName(label); header->addWidget(button); return button;
    };
    view.grip=UiControls::toolButton(editor->findChild<QWidget*>("editorLineNumberGutter"));
    view.grip->setObjectName("editorBlockDragHandle");view.grip->setProperty("blockId",block.id);
    view.grip->setIcon(RoundedIcons::icon(RoundedIcons::Grip));view.grip->setIconSize({16,16});view.grip->setFixedSize(26,26);
    view.grip->setCursor(Qt::OpenHandCursor);view.grip->setToolTip("Select or drag block");view.grip->setAccessibleName("Select or drag block");
    connect(view.grip,&QToolButton::clicked,this,[this,id=block.id]{select(id);});
    view.edit=UiControls::toolButton(view.frame);frame->edit=view.edit;
    view.edit->setObjectName("blockEditToggle");view.edit->setIcon(RoundedIcons::icon(RoundedIcons::BlockEdit));
    view.edit->setIconSize({16,16});view.edit->setFixedSize(26,26);view.edit->setCheckable(true);
    view.edit->setToolTip("Edit block");view.edit->setAccessibleName("Edit block");
    connect(view.edit,&QToolButton::clicked,this,[this,id=block.id](bool edit){
        const auto value=views.value(id);if(value.content)value.content->setEditing(edit);refreshControls();
    });
    view.edit->hide();view.grip->hide();
    auto* menu = UiControls::menu(view.frame);
    auto* actions = tool(RoundedIcons::More, "Block actions"); actions->setMenu(menu); actions->setPopupMode(QToolButton::InstantPopup);
    const auto id=block.id;
    view.collapse=tool(RoundedIcons::CollapseBlock,"Collapse or expand this block");
    view.collapse->setEnabled(!editor->documentReadOnly());
    connect(view.collapse,&QToolButton::clicked,this,[this,id] {
        if(editor->documentReadOnly() || !model->flushContents())return;
        if(const auto* current=model->find(id)){auto next=*current;next.collapsed=!next.collapsed;model->update(next);}
    });
    populateBlockMenu(menu,id);
    auto* remove = tool(RoundedIcons::Delete, "Delete block"); remove->setEnabled(!editor->documentReadOnly());
    connect(remove, &QToolButton::clicked, this, [this,id] { if (!editor->documentReadOnly() && model->flushContents()) { model->remove(id); editor->setFocus(); } });
    view.content = retained ? retained : new EditorBlockContent(model, client, block, view.frame);
    view.content->openOriginal = [this,id] { openOriginal(id); };
    view.text = view.content->textEditor(); layout->addWidget(view.content, 1);
    view.tools->hide();
    return view;
}
void EditorBlockHost::populateBlockMenu(QMenu* menu,const QString& id) {
    const auto* found=model?model->find(id):nullptr;if(!menu || !found)return;
    const auto block=*found;
    auto button = [&](const QString& text, const QString& tooltip, auto action) {
        auto* item=menu->addAction(text);item->setToolTip(tooltip);
        item->setEnabled(!editor->documentReadOnly() || text=="Copy");
        connect(item,&QAction::triggered,this,[this,id,text,action] {
            if(model->find(id) && (text=="Copy" || !editor->documentReadOnly()) && model->flushContents())action();
        });
    };
    button("Attach", "Attach to the current code selection or line", [this,id] { model->reattach(id, editor->textCursor()); });
    button("Copy", "Copy block", [this,id] { emit editor->editorBlockClipboardRequested(id,false); });
    button("Cut", "Move this block when pasted", [this,id] { emit editor->editorBlockClipboardRequested(id,true); });
    button("Delete", "Remove this block (undoable)", [this,id] { model->remove(id); editor->setFocus(); });
    EditorBlockContent::addAppearanceMenu(menu,model,id,menu,[this] { return editor->documentReadOnly(); });
    DocumentReferenceService::addMenu(menu,model,id,menu,[this] { return editor->documentReadOnly(); });
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
}
void EditorBlockHost::updateVisible() {
    if (updating || !model) return;
    if (!editor->isVisible() || !editor->associatedBlocksVisible()) {
        for(auto& view:views){view.frame->hide();view.grip->hide();}
        return;
    }
    updating = true;
    editor->state->projection.ensure(editor, editor->state->folding.collapsedLineRanges());
    editor->state->projection.prepareVisibleGeometry(editor);
    const auto demanded = editor->state->projection.visibleAssociatedBlocks(editor, editor->viewport()->height());
    QSet<QString> keep;
    for (const auto& item : demanded) {
        if (!model->find(item.first)) continue;
        const auto presented=model->presentation(item.first); const auto* block=&presented;
        keep.insert(item.first);
        if (!views.contains(item.first)) views.insert(item.first, createView(*block));
        auto& view = views[item.first];
        if (view.revision != block->revision || view.styleRevision != BlockNoteStyle::instance().revision()) {
            view.collapse->setIcon(RoundedIcons::icon(block->collapsed ? RoundedIcons::ExpandBlock : RoundedIcons::CollapseBlock));
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
        const auto title=block->type=="reference"
            ? (block->caption.isEmpty()?block->uri:block->caption)
            : block->type=="text"?QString("Text note"):block->type;
        view.title->setTextFormat(block->type=="reference" && compact ? Qt::RichText : Qt::PlainText);
        view.title->setText(block->type=="reference" && compact
            ? QString("<a href=\"original\" style=\"color:%1\">%2</a>%3").arg(InsightVisualStyle::theme().accent.name(),(title.isEmpty()?QString("Reference"):title).toHtmlEscaped(),block->referenceStatus.isEmpty()?QString():" · "+block->referenceStatus.toHtmlEscaped())
            : QString(block->missing?"Missing anchor · ":"")+title);
        view.title->setToolTip(block->uri+"\n"+block->referenceStatus);
        view.frame->layout()->setContentsMargins(compact?QMargins(36,4,100,4):BlockShellMetrics::contents);
        view.title->setVisible(compact || block->missing);
        if(view.content)view.content->present(*block, editor->documentReadOnly(), compact);
        view.frame->setGeometry(item.second.toAlignedRect());
        view.frame->show();
        const auto top=view.grip->parentWidget()->mapFromGlobal(view.frame->mapToGlobal(QPoint())).y();
        view.grip->move(2,top+(compact?4:30));
    }
    for (auto it = views.begin(); it != views.end();) {
        const bool focused = it->frame == QApplication::focusWidget() || it->frame->isAncestorOf(QApplication::focusWidget());
        if (!model->find(it.key()) || model->find(it.key())->detached) {
            if (focused) editor->setFocus();
            auto* frame = it->frame; delete it->grip; it = views.erase(it); frame->deleteLater();
        } else {if(!keep.contains(it.key())){it->frame->hide();it->grip->hide();} ++it;}
    }
    updating = false; refreshControls();
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
        block.height=240;block.autoHeight=true;
    } else if (type == "reference") {
        const auto chosen=DocumentReferenceService::pick(client,editor,preset.value("uri").toString());
        if (!chosen.accepted || !current()) return;
        block.uri = chosen.uri; block.reference=DocumentBlockModel::positionReference();
        if (block.caption.isEmpty()) block.caption = chosen.caption;
        if (block.uri.isEmpty()) return;
        block.autoHeight = !preset.contains("height") && !EditorBlockPreview::linkOnlyText(block).isEmpty();
    }
    if (!current()) return;
    const auto id = model->insert(block, cursor);
    select(id);
}
void EditorBlockHost::select(const QString& id, bool edit) {
    const auto* block = model->find(id); if (!block) return;
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
        refreshControls();
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
    if(!editor->associatedBlocksVisible())return false;
    if (!model || model->items().isEmpty() || editor->textCursor().hasSelection() || event->modifiers() != Qt::NoModifier
        || (event->key() != Qt::Key_Up && event->key() != Qt::Key_Down)) return false;
    const auto cursor = editor->textCursor(); auto* layout = cursor.block().layout();
    const auto visual = layout ? layout->lineForTextPosition(cursor.positionInBlock()) : QTextLine();
    const bool forward = event->key() == Qt::Key_Down;
    if (visual.isValid() && ((forward && visual.lineNumber() + 1 < layout->lineCount()) || (!forward && visual.lineNumber() > 0))) return false;
    editor->state->projection.ensure(editor, editor->state->folding.collapsedLineRanges());
    const auto id = editor->state->projection.adjacentAssociatedBlock(cursor.blockNumber(), forward);
    if (id.isEmpty()) return false;
    select(id); return true;
}
bool EditorBlockHost::eventFilter(QObject* object, QEvent* event) {
    if (!model) return false;
    if (object == editor && (event->type() == QEvent::Hide || event->type() == QEvent::Show)) schedule();
    if (object == editor && event->type() == QEvent::FocusIn) { selectedId.clear(); refreshControls(); }
    if (event->type() == QEvent::Resize && object == editor->viewport()) schedule();
    auto* widget = qobject_cast<QWidget*>(object); if (!widget) return false;
    QString id;
    for (auto it = views.cbegin(); it != views.cend(); ++it) if (widget == it->grip || widget == it->frame || it->frame->isAncestorOf(widget)) { id = it.key(); break; }
    if (id.isEmpty()) { if(event->type()==QEvent::MouseMove || event->type()==QEvent::Enter || event->type()==QEvent::Leave) refreshControls(); return false; }
    const auto* block = model->find(id); if (!block) return false;
    const auto view = views.value(id);
    if (event->type() == QEvent::MouseMove || event->type() == QEvent::Enter || event->type() == QEvent::Leave
        || event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut) {
        const QPointer<QWidget> frame = view.frame, tools = view.tools;
        QTimer::singleShot(0, this, [this,frame,tools] {
            Q_UNUSED(frame) Q_UNUSED(tools)
            refreshControls();
        });
    }
    if (event->type() == QEvent::FocusOut) view.frame->update();
    if (event->type() != QEvent::KeyPress) return false;
    auto* key = static_cast<QKeyEvent*>(event);
    if (key->matches(QKeySequence::Undo) || key->matches(QKeySequence::Redo)) { if(view.content && view.content->nativeWaveEditing()) return false; if(!finishEditing())return true; if (key->matches(QKeySequence::Undo)) editor->undo(); else editor->redo(); return true; }
    if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) {
        if (view.content && view.content->isEditing()) return false;
        view.tools->show();
        QList<QWidget*> controls{view.grip,view.edit};
        for (auto* control : view.tools->findChildren<QWidget*>()) if (control->isVisible() && control->isEnabled() && (control->focusPolicy() & Qt::TabFocus)) controls.append(control);
        if (!controls.isEmpty()) { int index = controls.indexOf(widget); const bool back = key->key() == Qt::Key_Backtab || key->modifiers().testFlag(Qt::ShiftModifier); index = (index + (back ? -1 : 1) + controls.size()) % controls.size(); controls[index]->setFocus(); }
        refreshControls();return true;
    }
    if (widget != view.frame && widget != view.grip) return false;
    // A focused block shell is not a source-text insertion point.
    if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter || key->key() == Qt::Key_Escape) {
        key->accept();
        return true;
    }
    if (key->key() == Qt::Key_Delete && !editor->documentReadOnly()) { if(!model->flushContents())return true; model->remove(id); editor->setFocus(); return true; }
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
