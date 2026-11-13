#include "associatedblockdragcontroller.h"
#include "tabmanager.h"
#include "associatedblockpage.h"
#include "shareddocument.h"
#include "documentblockmodel.h"
#include "editorblockcontent.h"
#include "blockdropadapter.h"
#include "documentblockstore.h"
#include "editorruntime.h"
#include "editordroppreviewoverlay.h"
#include "insightvisualstyle.h"
#include <QApplication>
#include <QAbstractButton>
#include <QDrag>
#include <QMimeData>
#include <QMouseEvent>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QLabel>
#include <QFrame>
#include <QTabBar>
#include <QTimer>
#include <QScrollBar>
#include <QTextDocument>
#include <QWindow>
#include <QScopeGuard>
#include <QClipboard>
#include <QJsonDocument>
#include <QUuid>
#include <limits>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
constexpr auto mimeType="application/x-zeroslack-associated-block";
constexpr auto clipboardType="application/x-zeroslack-block+json";
MyCodeEditor* editorAncestor(QWidget* widget) {
    for (auto* p=widget;p;p=p->parentWidget()) if (auto* editor=qobject_cast<MyCodeEditor*>(p)) return editor;
    return nullptr;
}
QWidget* blockDragSource(QWidget* widget) {
    for (auto* p=widget;p;p=p->parentWidget()) {
        if (p->objectName()=="editorBlockDragHandle" || p->objectName()=="associatedBlockDragHandle") return p;
        if (qobject_cast<QAbstractButton*>(p) || p->objectName()=="editorBlockTools" || p->objectName()=="associatedBlockTools") return nullptr;
        if (p->objectName()=="editorBlockShell" || p->objectName()=="associatedBlockPage") {
            auto* child=p->findChild<QWidget*>("editorBlockContent");
            auto* content=child ? static_cast<EditorBlockContent*>(child) : nullptr;
            return content && !content->isEditing() ? p : nullptr;
        }
    }
    return nullptr;
}
bool releasedOutside(bool cancelled) {
    if (cancelled) return false;
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName()=="windows")
        return !(GetAsyncKeyState(VK_LBUTTON)&0x8000) && !(GetAsyncKeyState(VK_ESCAPE)&0x8000);
#endif
    return !(QApplication::mouseButtons()&Qt::LeftButton);
}
Qt::DropAction requestedAction() { return QApplication::keyboardModifiers().testFlag(Qt::ControlModifier) ? Qt::CopyAction : Qt::MoveAction; }
}
AssociatedBlockDragController::AssociatedBlockDragController(TabManager* value) : QObject(value),manager(value),scrollTimer(new QTimer(this)),presentationTimer(new QTimer(this)) {
    setObjectName("associatedBlockDragController"); qApp->installEventFilter(this);
    presentationTimer->setInterval(40);
    connect(presentationTimer,&QTimer::timeout,this,[this] {
        if (!dragInProgress() || !valid(activeMime)) { restoreSource(); return; }
        const auto point=QCursor::pos();
        for (auto* group:manager->editorSplitController()->groups()) {
            if (group->isVisible()) {
                if (auto* editor=qobject_cast<MyCodeEditor*>(group->currentWidget())) {
                    auto* gutter=editor->findChild<QWidget*>("editorLineNumberGutter");
                    if (gutter && QRect(gutter->mapToGlobal(QPoint()),gutter->size()).contains(point)) {
                        proposedAction=requestedAction();
                        if(proposedAction==Qt::MoveAction && owner->readOnly()) { clearPreview(); return; }
                        if (gutterTarget(editor,point)) concealSource();
                        return;
                    }
                }
            }
        }
        restoreSource();
    });
    scrollTimer->setInterval(40);
    connect(scrollTimer,&QTimer::timeout,this,[this] {
        if (!targetEditor || (!externalPreview && !valid(activeMime))) { clearPreview(); return; }
        const auto local=targetEditor->viewport()->mapFromGlobal(lastGlobal);
        const int direction=local.y()<24 ? -1 : local.y()>targetEditor->viewport()->height()-24 ? 1 : 0;
        if (!direction) return;
        auto* scroll=targetEditor->verticalScrollBar();
        scroll->setValue(qBound(scroll->minimum(),scroll->value()+direction*qMax(1,scroll->singleStep()),scroll->maximum()));
        gutterTarget(targetEditor,lastGlobal,externalPreview);
    });
    connect(manager->editorSplitController(),&EditorSplitController::tabDragStarted,this,[this](QDrag* drag) {
        auto* page=drag ? qobject_cast<AssociatedBlockPage*>(drag->property("ElaHostedTabPage").value<QWidget*>()) : nullptr;
        if (page) { tabDragging=true; nativeDrag=drag; dragImage=drag->pixmap(); arm(const_cast<QMimeData*>(drag->mimeData()),page->ownerDocument(),page->blockId()); presentationTimer->start(); }
    });
    connect(manager->editorSplitController(),&EditorSplitController::tabDragFinished,this,[this] { tabDragging=false; presentationTimer->stop(); clearPreview(); nativeDrag.clear(); activeMime.clear(); emit gestureFinished(); });
}
AssociatedBlockDragController::~AssociatedBlockDragController() { if(dragInProgress()) QDrag::cancel(); qApp->removeEventFilter(this); clearPreview(); delete splitPreview; }
void AssociatedBlockDragController::attachEditor(MyCodeEditor* editor) {
    if (auto* gutter=editor->findChild<QWidget*>("editorLineNumberGutter")) gutter->setAcceptDrops(true);
    connect(editor,&MyCodeEditor::editorBlockClipboardRequested,this,[this,editor](const QString& id,bool cut) {
        copyBlock(manager->sharedDocumentForEditor(editor),id,cut);
    });
}
void AssociatedBlockDragController::arm(QMimeData* mime, SharedDocument* document, const QString& blockId) {
    owner=document; id=blockId; activeMime=mime;
    if (!document || !mime) return;
    auto* model=DocumentBlockModel::get(document->textDocument()); model->endTextSession();
    sourceSnapshot=DocumentBlockTransfer::capture(document,blockId);
    mime->setData(mimeType,blockId.toUtf8());
}
QMimeData* AssociatedBlockDragController::createMimeData(SharedDocument* document, const QString& blockId) {
    auto* mime=new QMimeData; arm(mime,document,blockId); return mime;
}
bool AssociatedBlockDragController::copyBlock(SharedDocument* document, const QString& blockId, bool cut) {
    if (!document) return false;
    auto* model=DocumentBlockModel::get(document->textDocument());
    if (cut && document->readOnly()) { model->setError("A read-only block can be copied, but not cut."); return false; }
    QString issue; auto snapshot=DocumentBlockTransfer::snapshot(document,blockId,&issue);
    if (snapshot.isEmpty()) { model->setError(issue); return false; }
    auto* mime=new QMimeData;
    snapshot["clipboardToken"]=QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto encoded=QJsonDocument(snapshot).toJson(QJsonDocument::Compact);
    mime->setData(clipboardType,encoded);
    const auto* block=model->find(blockId); mime->setText(block->type=="text" || block->type=="code" ? block->text : block->uri);
    QApplication::clipboard()->setMimeData(mime);
    cutPayload.clear(); cutSource={};
    const auto* published=QApplication::clipboard()->mimeData();
    if (!published || published->data(clipboardType)!=encoded) { model->setError("The system clipboard is unavailable. The original block was preserved."); return false; }
    cutPayload=cut ? encoded : QByteArray(); cutSource=cut ? DocumentBlockTransfer::capture(document,blockId) : BlockTransferSource{};
    return true;
}
bool AssociatedBlockDragController::pasteBlock(MyCodeEditor* editor, const QTextCursor& destination, const QString& beforeId) {
    const auto* mime=QApplication::clipboard()->mimeData();
    auto* document=editor ? manager->sharedDocumentForEditor(editor) : nullptr;
    if (!document || !mime || !mime->hasFormat(clipboardType)) return false;
    QString issue, placed;
    const bool pendingCut=!cutPayload.isEmpty() && mime->data(clipboardType)==cutPayload;
    if (pendingCut) placed=DocumentBlockTransfer::apply(cutSource,document,destination,DocumentBlockTransfer::Mode::Move,beforeId,&issue);
    else {
        const auto bytes=mime->data(clipboardType);
        QJsonParseError parse;
        const auto snapshot=bytes.size()<=72*1024*1024 ? QJsonDocument::fromJson(bytes,&parse).object() : QJsonObject();
        if (snapshot.isEmpty() || parse.error!=QJsonParseError::NoError) issue="Invalid or oversized block clipboard data.";
        else placed=DocumentBlockTransfer::paste(snapshot,document,destination,beforeId,&issue);
    }
    if (placed.isEmpty()) {
        emit editor->editorStatusMessageRequested(issue.isEmpty() ? "The block could not be pasted. Both documents were preserved." : issue);
        return false;
    }
    if (pendingCut) { cutPayload.clear(); cutSource={}; }
    editor->selectAssociatedBlock(placed); return true;
}
bool AssociatedBlockDragController::valid(const QMimeData* mime) const {
    return mime && mime==activeMime && DocumentBlockTransfer::current(sourceSnapshot);
}
QTabWidget* AssociatedBlockDragController::groupAt(QWidget* widget, const QPoint& global) const {
    const auto groups=manager->editorSplitController()->groups();
    for (auto* p=widget;p;p=p->parentWidget()) if (auto* group=qobject_cast<QTabWidget*>(p); group && groups.contains(group)) return group;
    for (auto* group:groups) if (group->isVisible() && widget && widget->window()==group->window()
        && QRect(group->mapToGlobal(QPoint()),group->size()).contains(global)) return group;
    return nullptr;
}
void AssociatedBlockDragController::clearPreview() {
    restoreSource();
    externalPreview=false;
    scrollTimer->stop(); targetEditor.clear(); targetCursor={}; beforeId.clear();
    delete marker; delete markerLine; marker=nullptr; markerLine=nullptr;
    if (splitPreview) splitPreview->clearPreview();
}
void AssociatedBlockDragController::restoreSource() {
    if (hiddenPresentation) { hiddenPresentation->show(); hiddenPresentation.clear(); }
    if (nativeDrag && nativeDrag->pixmap().isNull()) nativeDrag->setPixmap(dragImage);
}
void AssociatedBlockDragController::concealSource() {
    if (hiddenPresentation) return;
    for (auto* page:manager->associatedBlockPages()) if (page->ownerDocument()==owner && page->blockId()==id && page->isVisible()) {
        QWidget* presentation=page;
        auto* sourceWindow=page->window();
        if (sourceWindow!=manager->editorSplitController()->initialGroup()->window()) {
            int pages=0;
            for (auto* group:manager->editorSplitController()->groups()) if (group->window()==sourceWindow) pages+=group->count();
            if (pages==1) presentation=sourceWindow;
        }
        hiddenPresentation=presentation; presentation->hide(); break;
    }
    if (nativeDrag) nativeDrag->setPixmap(QPixmap());
}
bool AssociatedBlockDragController::gutterTarget(MyCodeEditor* editor, const QPoint& global, bool external) {
    if ((!external && !valid(activeMime)) || !editor || editor->documentReadOnly() || !manager->sharedDocumentForEditor(editor)) { clearPreview(); return false; }
    externalPreview=external;
    const QPoint local=editor->viewport()->mapFromGlobal(global);
    if (local.y()<0 || local.y()>=editor->viewport()->height()) { clearPreview(); return false; }
    auto* projection=&editor->state->projection;
    projection->ensure(editor,editor->state->folding.collapsedLineRanges());
    projection->prepareVisibleGeometry(editor);
    auto* model=editor->blockModel();
    const auto excluded=!external && proposedAction==Qt::MoveAction && manager->sharedDocumentForEditor(editor)==owner ? id : QString();
    const auto anchorLine=[&](const auto& block) {
        return block.missing ? editor->blockCount()-1 : editor->document()->findBlock(qMax(block.start,block.end-1)).blockNumber();
    };
    QTextCursor cursor=editor->cursorForPosition({0,local.y()});
    int line=cursor.blockNumber(); qreal edge=editor->blockGeometry(line).top+editor->blockGeometry(line).height;
    QString before; bool overGap=false;
    const auto gaps=projection->visibleAssociatedBlocks(editor,0);
    for (const auto& gap:gaps) if (gap.second.top()<=local.y() && gap.second.bottom()>=local.y()) {
        overGap=true;
        const auto* anchor=model->find(gap.first); if (!anchor) { clearPreview(); return false; }
        line=anchorLine(*anchor);
        if (local.y()<gap.second.center().y()) { before=gap.first; edge=gap.second.top(); }
        else { edge=gap.second.bottom();
            const auto next=projection->neighboringAssociatedBlock(gap.first,true);
            if (const auto* sibling=model->find(next); sibling && anchorLine(*sibling)==line) before=next;
        }
        break;
    }
    if (!overGap) {
        int firstOrder=std::numeric_limits<int>::max();
        for (const auto& block:model->items())
            if (!block.detached && block.id!=excluded && anchorLine(block)==line
                && block.order<firstOrder) { before=block.id; firstOrder=block.order; }
    }
    const auto source=editor->document()->findBlockByNumber(line);
    if (!source.isValid() || (!excluded.isEmpty() && before==excluded)) { clearPreview(); return false; }
    targetCursor=QTextCursor(source); beforeId=before;
    if (targetEditor!=editor || !marker) {
        delete marker; delete markerLine;
        marker=new QLabel(editor->viewport()); marker->setObjectName("associatedBlockDropMarker"); marker->setAttribute(Qt::WA_TransparentForMouseEvents);
        markerLine=new QFrame(editor->viewport()); markerLine->setAttribute(Qt::WA_TransparentForMouseEvents);
        targetEditor=editor;
    }
    int position=1;
    const auto* beforeBlock=model->find(before);
    for (const auto& block:model->items())
        if (!block.detached && block.id!=excluded && anchorLine(block)==line && (!beforeBlock || block.order<beforeBlock->order)) ++position;
    marker->setText(QString("After line %1 · position %2").arg(line+1).arg(position));
    marker->setProperty("sourceLine",line); marker->setProperty("beforeBlockId",before);
    const auto& theme=InsightVisualStyle::theme();
    marker->setStyleSheet(QString("background:%1;color:%2;padding:3px 6px;border:1px solid %3;border-radius:4px;")
        .arg(theme.panelBackground.name(),theme.textPrimary.name(),theme.accent.name()));
    marker->adjustSize(); marker->move(6,qBound(0,int(edge)-marker->height()-3,qMax(0,editor->viewport()->height()-marker->height())));
    markerLine->setStyleSheet("background:"+theme.accent.name()); markerLine->setGeometry(0,qBound(0,int(edge),editor->viewport()->height()-2),editor->viewport()->width(),2);
    marker->show(); markerLine->show(); marker->raise(); markerLine->raise();
    if (splitPreview) splitPreview->clearPreview(); lastGlobal=global; scrollTimer->start(); if(!external) concealSource(); return true;
}
void AssociatedBlockDragController::start(QWidget* handle, Qt::KeyboardModifiers modifiers) {
    SharedDocument* document=nullptr; QString blockId;
    for (auto* p=handle;p;p=p->parentWidget()) {
        if (auto* page=qobject_cast<AssociatedBlockPage*>(p)) { document=page->ownerDocument(); blockId=page->blockId(); break; }
        if (blockId.isEmpty()) blockId=p->property("blockId").toString();
        if (auto* editor=qobject_cast<MyCodeEditor*>(p)) { document=manager->sharedDocumentForEditor(editor); break; }
    }
    if (!document || !DocumentBlockModel::get(document->textDocument())->find(blockId)) return;
    QPointer<QDrag> drag=new QDrag(this); drag->setMimeData(createMimeData(document,blockId));
    drag->setPixmap(handle->grab()); nativeDrag=drag; dragImage=drag->pixmap(); cancelled=false; dragging=true; pressed.clear(); presentationTimer->start();
    QPointer<AssociatedBlockPage> sourcePage;
    for(auto* p=handle;p;p=p->parentWidget()) if((sourcePage=qobject_cast<AssociatedBlockPage*>(p))) break;
    QPointer<QTabWidget> sourceGroup=sourcePage ? manager->editorSplitController()->groupForPage(sourcePage) : nullptr;
    if(sourceGroup && sourceGroup->currentWidget()==sourcePage) {
        MyCodeEditor* fallback=nullptr;
        for(int i=0;i<sourceGroup->count();++i) if(auto* code=qobject_cast<MyCodeEditor*>(sourceGroup->widget(i))) {
            if(!fallback) fallback=code;
            if(manager->sharedDocumentForEditor(code)==document) { fallback=code; break; }
        }
        if(fallback) sourceGroup->setCurrentWidget(fallback);
    }
    const QPointer<AssociatedBlockDragController> self(this);
    Qt::DropAction result=Qt::IgnoreAction;
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName()=="windows") {
        const QPointer<QWindow> input=handle->window()->windowHandle();
        if (GetAsyncKeyState(VK_ESCAPE)&0x8000) cancelled=true;
        else if (!(GetAsyncKeyState(VK_LBUTTON)&0x8000)) {
            // A fast gesture can finish before Qt enters its native drag loop.
            // Commit through the same target validation as a native drop.
            const auto global=QCursor::pos();
            const auto action=modifiers.testFlag(Qt::ControlModifier) ? Qt::CopyAction : Qt::MoveAction;
            if (routeTarget(QApplication::widgetAt(global),global,true,action)) result=action;
        } else if (input) {
            const auto hwnd=reinterpret_cast<HWND>(input->winId());
            SetFocus(hwnd);
            if (input->isVisible()) input->setMouseGrabEnabled(true);
            const auto releaseCapture=qScopeGuard([input] { if(input && input->isVisible()) input->setMouseGrabEnabled(false); });
            POINT cursor{}; GetCursorPos(&cursor); ScreenToClient(hwnd,&cursor);
            // Qt 6.10 starts OLE only after a move reaches its input window.
            PostMessageW(hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(cursor.x,cursor.y));
            bool released=false; QPoint releasePosition;
            QTimer releaseCheck;
            releaseCheck.setInterval(10);
            connect(&releaseCheck,&QTimer::timeout,&releaseCheck,[&] {
                if (!self || !drag) { releaseCheck.stop(); return; }
                if (GetAsyncKeyState(VK_ESCAPE)&0x8000) cancelled=true;
                if (cancelled) { releaseCheck.stop(); QDrag::cancel(); }
                else if (!(GetAsyncKeyState(VK_LBUTTON)&0x8000)) {
                    // Release can also arrive while Qt is entering OLE.
                    // End that loop before committing the observed release.
                    released=true; releasePosition=QCursor::pos();
                    releaseCheck.stop(); QDrag::cancel();
                }
            });
            releaseCheck.start();
            result=drag->exec(Qt::MoveAction|Qt::CopyAction,Qt::MoveAction);
            releaseCheck.stop();
            if (self && drag && released && !cancelled && result==Qt::IgnoreAction
                && routeTarget(QApplication::widgetAt(releasePosition),releasePosition,true,requestedAction())) result=requestedAction();
        } else cancelled=true;
    } else
#endif
        result=drag->exec(Qt::MoveAction|Qt::CopyAction,Qt::MoveAction);
    if(!self || !drag) return;
    dragging=false; presentationTimer->stop(); clearPreview();
    if (result==Qt::IgnoreAction && valid(drag->mimeData()) && releasedOutside(cancelled)) {
        auto* hit=QApplication::widgetAt(QCursor::pos());
        // Releasing over any application surface is an invalid drop, not a float gesture.
        bool overApplication=false;
        for (auto* group:manager->editorSplitController()->groups())
            if (group->window()->isVisible() && group->window()->frameGeometry().contains(QCursor::pos())) overApplication=true;
        if (!overApplication && !groupAt(hit,QCursor::pos())) {
            manager->detachAssociatedBlock(owner,id,nullptr,[this](auto* page) {
                return manager->editorSplitController()->floatPage(page,QCursor::pos())!=nullptr;
            },requestedAction()==Qt::CopyAction);
        }
    }
    if(result==Qt::IgnoreAction && sourcePage && sourceGroup && sourceGroup->indexOf(sourcePage)>=0) {
        sourceGroup->setCurrentWidget(sourcePage); sourcePage->setFocus(Qt::OtherFocusReason);
    }
    activeMime.clear(); nativeDrag.clear(); if (drag) drag->deleteLater(); emit gestureFinished();
}
bool AssociatedBlockDragController::eventFilter(QObject* watched, QEvent* event) {
    auto* widget=qobject_cast<QWidget*>(watched);
    if (dragging && event->type()==QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape) cancelled=true;
    if (!widget) return false;
    if (auto* bar=qobject_cast<QTabBar*>(widget)) {
        if (event->type()==QEvent::MouseButtonPress) {
            auto* mouse=static_cast<QMouseEvent*>(event);
            if (mouse->button()==Qt::LeftButton) for (auto* group:manager->editorSplitController()->groups()) {
                if (group->tabBar()!=bar) continue;
                const auto index=bar->tabAt(mouse->position().toPoint());
                if (auto* page=qobject_cast<AssociatedBlockPage*>(group->widget(index))) {
                    pressed=bar; pressedTabPage=page; pressedAt=mouse->globalPosition().toPoint();
                    group->setCurrentIndex(index); manager->editorSplitController()->setActiveGroup(group);
                    mouse->accept(); return true;
                }
            }
        } else if (event->type()==QEvent::MouseMove && pressed==bar && pressedTabPage) {
            auto* mouse=static_cast<QMouseEvent*>(event);
            if ((mouse->buttons()&Qt::LeftButton) && (mouse->globalPosition().toPoint()-pressedAt).manhattanLength()>=QApplication::startDragDistance()) {
                const auto page=pressedTabPage; pressedTabPage.clear(); start(page,mouse->modifiers()); return true;
            }
        }
    }
    const bool input=event->type()==QEvent::MouseButtonPress || event->type()==QEvent::MouseMove
        || event->type()==QEvent::KeyPress || event->type()==QEvent::ShortcutOverride;
    // Child removal events can arrive while a QWidget's layout is partially destroyed.
    // Only inspect the content tree for actual input, never lifecycle notifications.
    auto* handle=input ? blockDragSource(widget) : nullptr;
    if (event->type()==QEvent::ShortcutOverride || event->type()==QEvent::KeyPress) {
        auto* key=static_cast<QKeyEvent*>(event);
        const bool copy=key->matches(QKeySequence::Copy), cut=key->matches(QKeySequence::Cut), paste=key->matches(QKeySequence::Paste);
        auto* code=editorAncestor(widget);
        const bool blockClipboard=QApplication::clipboard()->mimeData() && QApplication::clipboard()->mimeData()->hasFormat(clipboardType);
        const bool codeTarget=code && (widget==code || widget==code->viewport());
        if ((handle && (copy || cut || (paste && blockClipboard))) || (codeTarget && paste && blockClipboard)) {
            key->accept();
            if (event->type()==QEvent::ShortcutOverride) return true;
            SharedDocument* document=nullptr; QString selected;
            for (auto* p=handle;p;p=p->parentWidget()) {
                if (auto* page=qobject_cast<AssociatedBlockPage*>(p)) { document=page->ownerDocument(); selected=page->blockId(); break; }
                if (selected.isEmpty()) selected=p->property("blockId").toString();
                if (auto* view=qobject_cast<MyCodeEditor*>(p)) { document=manager->sharedDocumentForEditor(view); break; }
            }
            if (copy || cut) copyBlock(document,selected,cut);
            else if (code) {
                if (handle) {
                    const auto* block=code->blockModel()->find(selected);
                    if (block) pasteBlock(code,QTextCursor(code->document()->findBlock(qMax(block->start,block->end-1))),selected);
                } else if (code->property("blockPasteRevision").isValid()
                    && code->property("blockPasteRevision").toULongLong()==code->semanticDocumentRevision()
                    && code->property("blockPasteLine").toInt()==code->textCursor().blockNumber()) pasteBlock(code,code->textCursor());
                else emit code->editorStatusMessageRequested("Select line numbers or a block to choose where to paste the block.");
            }
            else if (document) DocumentBlockModel::get(document->textDocument())->setError("Choose line numbers in a code document, then paste the block.");
            return true;
        }
    }
    if (handle && event->type()==QEvent::MouseButtonPress) {
        auto* mouse=static_cast<QMouseEvent*>(event); if (mouse->button()==Qt::LeftButton) { pressed=handle; pressedAt=mouse->globalPosition().toPoint(); }
    } else if (handle && pressed==handle && event->type()==QEvent::MouseMove) {
        auto* mouse=static_cast<QMouseEvent*>(event);
        if ((mouse->buttons()&Qt::LeftButton) && (mouse->globalPosition().toPoint()-pressedAt).manhattanLength()>=QApplication::startDragDistance()) { start(handle,mouse->modifiers()); return true; }
    } else if (event->type()==QEvent::MouseButtonRelease) { pressed.clear(); pressedTabPage.clear(); }
    const bool move=event->type()==QEvent::DragEnter || event->type()==QEvent::DragMove, drop=event->type()==QEvent::Drop;
    if (!move && !drop) { if(event->type()==QEvent::DragLeave) clearPreview(); return false; }
    auto* e=static_cast<QDropEvent*>(event);
    const auto global=widget->mapToGlobal(e->position().toPoint());
    auto* hit=widget->objectName()=="editorLineNumberGutter" ? widget : widget->childAt(e->position().toPoint());
    const bool gutter=hit && hit->objectName()=="editorLineNumberGutter";
    if (!e->mimeData()->hasFormat(mimeType)) {
        if (!gutter) return false;
        auto* editor=editorAncestor(hit);
        if (!(e->possibleActions()&Qt::CopyAction) || !BlockDropAdapter::accepts(e->mimeData()) || !gutterTarget(editor,global,true)) { e->ignore(); clearPreview(); return true; }
        if (drop) {
            auto* document=manager->sharedDocumentForEditor(editor); auto* model=editor->blockModel();
            const auto revision=model->revision(),textRevision=document->textRevision();
            QString issue; auto* store=DocumentBlockStore::get(model);
            if (!DocumentBlockTransfer::validateParticipant(document,true,&issue)) {
                if(issue.isEmpty()) issue="Resolve the external document change before inserting blocks.";
            } else {
                const auto values=BlockDropAdapter::decode(e->mimeData(),store,&issue);
                if (!values.isEmpty() && model->revision()==revision && document->textRevision()==textRevision && !document->readOnly()) {
                    const auto placed=model->insertMany(values,targetCursor,beforeId);
                    if(!placed.isEmpty()) editor->selectAssociatedBlock(placed.last()); else issue="The insertion target changed.";
                } else if(issue.isEmpty()) issue="The dropped data or insertion target is no longer available.";
            }
            clearPreview();
            if(!issue.isEmpty()) { emit editor->editorStatusMessageRequested(issue); e->ignore(); return true; }
        }
        e->setDropAction(Qt::CopyAction); e->accept(); return true;
    }
    // Existing Ela tab gestures keep their standard tab/split/floating routing.
    if (!gutter && e->mimeData()->hasFormat("application/x-ela-hosted-tab")) return false;
    const auto action=e->modifiers().testFlag(Qt::ControlModifier) ? Qt::CopyAction : Qt::MoveAction;
    if (!(e->possibleActions()&action) || !valid(e->mimeData()) || !routeTarget(hit ? hit : widget,global,drop,action)) { e->ignore(); clearPreview(); return true; }
    e->setDropAction(action); e->accept(); return true;
}
bool AssociatedBlockDragController::routeTarget(QWidget* widget, const QPoint& global, bool drop, Qt::DropAction action) {
    if (!widget || !valid(activeMime) || (action==Qt::MoveAction && owner->readOnly())) { clearPreview(); return false; }
    proposedAction=action;
    if (widget->objectName()=="editorLineNumberGutter") {
        if (!gutterTarget(editorAncestor(widget),global)) return false;
        if (drop) {
            const auto editor=targetEditor; const auto cursor=targetCursor; const auto before=beforeId;
            QString issue;
            const auto placed=DocumentBlockTransfer::apply(sourceSnapshot,manager->sharedDocumentForEditor(editor),cursor,
                action==Qt::CopyAction ? DocumentBlockTransfer::Mode::Copy : DocumentBlockTransfer::Mode::Move,before,&issue);
            if (placed.isEmpty()) { if (editor && !issue.isEmpty()) emit editor->editorStatusMessageRequested(issue); clearPreview(); return false; }
            clearPreview();
            if (editor) {
                const auto blockId=placed;
                QTimer::singleShot(0,editor,[editor,blockId] {
                    const auto* block=editor->blockModel()->find(blockId);
                    if (block && !block->detached && editor->isVisible()) editor->selectAssociatedBlock(blockId);
                });
            }
        }
    } else {
        auto* group=groupAt(widget,global); if (!group) { clearPreview(); return false; }
        clearPreview();
        bool overTabBar=false;
        for(auto* p=widget;p && p!=group;p=p->parentWidget()) if(qobject_cast<QTabBar*>(p)) { overTabBar=true; break; }
        const auto direction=overTabBar ? EditorSplitDirection::Center : EditorDropPreviewOverlay::directionAt(group->rect(),group->mapFromGlobal(global));
        if (drop) {
            auto* page=manager->detachAssociatedBlock(owner,id,group,[this,group,direction](auto* page) {
                return direction!=EditorSplitDirection::Center
                    ? manager->editorSplitController()->movePageToSplit(page,direction,group)!=nullptr
                    : manager->editorSplitController()->movePage(page,group);
            },action==Qt::CopyAction);
            if (!page) return false;
        } else {
            if (!splitPreview) splitPreview=new EditorDropPreviewOverlay(manager->editorSplitController()->host());
            splitPreview->showPreview(group,direction);
        }
    }
    return true;
}
