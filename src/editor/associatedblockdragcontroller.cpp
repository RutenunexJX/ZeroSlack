#include "associatedblockdragcontroller.h"
#include "tabmanager.h"
#include "associatedblockpage.h"
#include "shareddocument.h"
#include "documentblockmodel.h"
#include "editorruntime.h"
#include "editordroppreviewoverlay.h"
#include "insightvisualstyle.h"
#include <QApplication>
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
#include <limits>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
constexpr auto mimeType="application/x-zeroslack-associated-block";
MyCodeEditor* editorAncestor(QWidget* widget) {
    for (auto* p=widget;p;p=p->parentWidget()) if (auto* editor=qobject_cast<MyCodeEditor*>(p)) return editor;
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
}
AssociatedBlockDragController::AssociatedBlockDragController(TabManager* value) : QObject(value),manager(value),scrollTimer(new QTimer(this)) {
    setObjectName("associatedBlockDragController"); qApp->installEventFilter(this);
    scrollTimer->setInterval(40);
    connect(scrollTimer,&QTimer::timeout,this,[this] {
        if (!targetEditor || !valid(activeMime)) { clearPreview(); return; }
        const auto local=targetEditor->viewport()->mapFromGlobal(lastGlobal);
        const int direction=local.y()<24 ? -1 : local.y()>targetEditor->viewport()->height()-24 ? 1 : 0;
        if (!direction) return;
        auto* scroll=targetEditor->verticalScrollBar();
        scroll->setValue(qBound(scroll->minimum(),scroll->value()+direction*qMax(1,scroll->singleStep()),scroll->maximum()));
        gutterTarget(targetEditor,lastGlobal);
    });
    connect(manager->editorSplitController(),&EditorSplitController::tabDragStarted,this,[this](QDrag* drag) {
        auto* page=drag ? qobject_cast<AssociatedBlockPage*>(drag->property("ElaHostedTabPage").value<QWidget*>()) : nullptr;
        if (page) { tabDragging=true; arm(const_cast<QMimeData*>(drag->mimeData()),page->ownerDocument(),page->blockId()); }
    });
    connect(manager->editorSplitController(),&EditorSplitController::tabDragFinished,this,[this] { tabDragging=false; clearPreview(); activeMime.clear(); emit gestureFinished(); });
}
AssociatedBlockDragController::~AssociatedBlockDragController() { if(dragInProgress()) QDrag::cancel(); qApp->removeEventFilter(this); clearPreview(); delete splitPreview; }
void AssociatedBlockDragController::attachEditor(MyCodeEditor* editor) {
    if (auto* gutter=editor->findChild<QWidget*>("editorLineNumberGutter")) gutter->setAcceptDrops(true);
}
void AssociatedBlockDragController::arm(QMimeData* mime, SharedDocument* document, const QString& blockId) {
    owner=document; id=blockId; activeMime=mime;
    if (!document || !mime) return;
    auto* model=DocumentBlockModel::get(document->textDocument()); model->endTextSession();
    identity=document->documentId(); serial=document->instanceSerial(); sourceRevision=document->textRevision(); associationRevision=model->revision();
    mime->setData(mimeType,blockId.toUtf8());
}
QMimeData* AssociatedBlockDragController::createMimeData(SharedDocument* document, const QString& blockId) {
    auto* mime=new QMimeData; arm(mime,document,blockId); return mime;
}
bool AssociatedBlockDragController::valid(const QMimeData* mime) const {
    return mime && mime==activeMime && owner && !owner->readOnly() && owner->documentId()==identity
        && owner->instanceSerial()==serial && owner->textRevision()==sourceRevision
        && DocumentBlockModel::get(owner->textDocument())->revision()==associationRevision
        && DocumentBlockModel::get(owner->textDocument())->find(id);
}
QTabWidget* AssociatedBlockDragController::groupAt(QWidget* widget, const QPoint& global) const {
    const auto groups=manager->editorSplitController()->groups();
    for (auto* p=widget;p;p=p->parentWidget()) if (auto* group=qobject_cast<QTabWidget*>(p); group && groups.contains(group)) return group;
    for (auto* group:groups) if (group->isVisible() && widget && widget->window()==group->window()
        && QRect(group->mapToGlobal(QPoint()),group->size()).contains(global)) return group;
    return nullptr;
}
void AssociatedBlockDragController::clearPreview() {
    scrollTimer->stop(); targetEditor.clear(); targetCursor={}; beforeId.clear();
    delete marker; delete markerLine; marker=nullptr; markerLine=nullptr;
    if (splitPreview) splitPreview->clearPreview();
}
bool AssociatedBlockDragController::gutterTarget(MyCodeEditor* editor, const QPoint& global) {
    if (!valid(activeMime) || !editor || editor->documentReadOnly() || manager->sharedDocumentForEditor(editor)!=owner) { clearPreview(); return false; }
    const QPoint local=editor->viewport()->mapFromGlobal(global);
    if (local.y()<0 || local.y()>=editor->viewport()->height()) { clearPreview(); return false; }
    auto* projection=&editor->state->projection;
    projection->ensure(editor,editor->state->folding.collapsedLineRanges());
    projection->prepareVisibleGeometry(editor);
    auto* model=DocumentBlockModel::get(owner->textDocument());
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
            if (!block.detached && block.id!=id && anchorLine(block)==line
                && block.order<firstOrder) { before=block.id; firstOrder=block.order; }
    }
    const auto source=editor->document()->findBlockByNumber(line);
    if (!source.isValid() || before==id) { clearPreview(); return false; }
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
        if (!block.detached && block.id!=id && anchorLine(block)==line && (!beforeBlock || block.order<beforeBlock->order)) ++position;
    marker->setText(QString("After line %1 · position %2").arg(line+1).arg(position));
    marker->setProperty("sourceLine",line); marker->setProperty("beforeBlockId",before);
    const auto& theme=InsightVisualStyle::theme();
    marker->setStyleSheet(QString("background:%1;color:%2;padding:3px 6px;border:1px solid %3;border-radius:4px;")
        .arg(theme.panelBackground.name(),theme.textPrimary.name(),theme.accent.name()));
    marker->adjustSize(); marker->move(6,qBound(0,int(edge)-marker->height()-3,qMax(0,editor->viewport()->height()-marker->height())));
    markerLine->setStyleSheet("background:"+theme.accent.name()); markerLine->setGeometry(0,qBound(0,int(edge),editor->viewport()->height()-2),editor->viewport()->width(),2);
    marker->show(); markerLine->show(); marker->raise(); markerLine->raise();
    if (splitPreview) splitPreview->clearPreview(); lastGlobal=global; scrollTimer->start(); return true;
}
void AssociatedBlockDragController::start(QWidget* handle) {
    SharedDocument* document=nullptr; QString blockId;
    for (auto* p=handle;p;p=p->parentWidget()) {
        if (auto* page=qobject_cast<AssociatedBlockPage*>(p)) { document=page->ownerDocument(); blockId=page->blockId(); break; }
        if (blockId.isEmpty()) blockId=p->property("blockId").toString();
        if (auto* editor=qobject_cast<MyCodeEditor*>(p)) { document=manager->sharedDocumentForEditor(editor); break; }
    }
    if (!document || document->readOnly() || !DocumentBlockModel::get(document->textDocument())->find(blockId)) return;
    QPointer<QDrag> drag=new QDrag(this); drag->setMimeData(createMimeData(document,blockId));
    drag->setPixmap(handle->grab()); cancelled=false; dragging=true; pressed.clear();
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
            if (routeTarget(QApplication::widgetAt(global),global,true)) result=Qt::MoveAction;
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
            result=drag->exec(Qt::MoveAction,Qt::MoveAction);
            releaseCheck.stop();
            if (self && drag && released && !cancelled && result==Qt::IgnoreAction
                && routeTarget(QApplication::widgetAt(releasePosition),releasePosition,true)) result=Qt::MoveAction;
        } else cancelled=true;
    } else
#endif
        result=drag->exec(Qt::MoveAction,Qt::MoveAction);
    if(!self || !drag) return;
    dragging=false; clearPreview();
    if (result==Qt::IgnoreAction && valid(drag->mimeData()) && releasedOutside(cancelled)) {
        auto* hit=QApplication::widgetAt(QCursor::pos());
        // Releasing over any application surface is an invalid drop, not a float gesture.
        bool overApplication=false;
        for (auto* group:manager->editorSplitController()->groups())
            if (group->window()->isVisible() && group->window()->frameGeometry().contains(QCursor::pos())) overApplication=true;
        if (!overApplication && !groupAt(hit,QCursor::pos())) {
            manager->detachAssociatedBlock(owner,id,nullptr,[this](auto* page) {
                return manager->editorSplitController()->floatPage(page,QCursor::pos())!=nullptr;
            });
        }
    }
    activeMime.clear(); if (drag) drag->deleteLater(); emit gestureFinished();
}
bool AssociatedBlockDragController::eventFilter(QObject* watched, QEvent* event) {
    auto* widget=qobject_cast<QWidget*>(watched);
    if (dragging && event->type()==QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape) cancelled=true;
    if (!widget) return false;
    const bool handle=widget->objectName()=="editorBlockDragHandle" || widget->objectName()=="associatedBlockDragHandle";
    if (handle && event->type()==QEvent::MouseButtonPress) {
        auto* mouse=static_cast<QMouseEvent*>(event); if (mouse->button()==Qt::LeftButton) { pressed=widget; pressedAt=mouse->globalPosition().toPoint(); }
    } else if (handle && pressed==widget && event->type()==QEvent::MouseMove) {
        auto* mouse=static_cast<QMouseEvent*>(event);
        if ((mouse->buttons()&Qt::LeftButton) && (mouse->globalPosition().toPoint()-pressedAt).manhattanLength()>=QApplication::startDragDistance()) { start(widget); return true; }
    } else if (event->type()==QEvent::MouseButtonRelease) pressed.clear();
    const bool move=event->type()==QEvent::DragEnter || event->type()==QEvent::DragMove, drop=event->type()==QEvent::Drop;
    if (!move && !drop) { if(event->type()==QEvent::DragLeave) clearPreview(); return false; }
    auto* e=static_cast<QDropEvent*>(event); if(!e->mimeData()->hasFormat(mimeType)) return false;
    const auto global=widget->mapToGlobal(e->position().toPoint());
    auto* hit=widget->objectName()=="editorLineNumberGutter" ? widget : widget->childAt(e->position().toPoint());
    const bool gutter=hit && hit->objectName()=="editorLineNumberGutter";
    // Existing Ela tab gestures keep their standard tab/split/floating routing.
    if (!gutter && e->mimeData()->hasFormat("application/x-ela-hosted-tab")) return false;
    if (!valid(e->mimeData()) || !routeTarget(hit ? hit : widget,global,drop)) { e->ignore(); clearPreview(); return true; }
    e->setDropAction(Qt::MoveAction); e->accept(); return true;
}
bool AssociatedBlockDragController::routeTarget(QWidget* widget, const QPoint& global, bool drop) {
    if (!widget || !valid(activeMime)) { clearPreview(); return false; }
    if (widget->objectName()=="editorLineNumberGutter") {
        if (!gutterTarget(editorAncestor(widget),global)) return false;
        if (drop) {
            const auto editor=targetEditor; const auto cursor=targetCursor; const auto before=beforeId;
            if (!valid(activeMime) || !DocumentBlockModel::get(owner->textDocument())->move(id,cursor,before)) { clearPreview(); return false; }
            clearPreview();
            if (editor) {
                const auto blockId=id;
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
            });
            if (!page) return false;
        } else {
            if (!splitPreview) splitPreview=new EditorDropPreviewOverlay(manager->editorSplitController()->host());
            splitPreview->showPreview(group,direction);
        }
    }
    return true;
}
