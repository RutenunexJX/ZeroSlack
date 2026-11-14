#include "tabmanager.h"
#include "associatedblockpage.h"
#include "associatedblockdragcontroller.h"
#include "documentblockmodel.h"
#include "documentblockstore.h"
#include "editorruntime.h"
#include "editorblockhost.h"
#include "editorblockcontent.h"
#include "uicontrols.h"
#include "roundedicons.h"
#include <QTextDocument>
#include <QDir>

void TabManager::transferAssociatedBlockContent(SharedDocument* source,DocumentBlockModel* target,const QString& id) {
    SharedDocument* destination=nullptr;
    for(auto* document:sharedDocuments->documents())if(DocumentBlockModel::get(document->textDocument())==target){destination=document;break;}
    if(!destination || !target->find(id))return;
    EditorBlockContent* content=nullptr;
    for(auto* page:associatedBlockPresentations())if(page->ownerDocument()==source && page->blockId()==id){content=page->takeContent();if(content)break;}
    if(!content)for(auto* view:source->views())if((content=view->takeAssociatedBlockContent(id)))break;
    if(!content)return;
    content->rebind(target);
    if(target->find(id)->detached){
        if(auto* page=createAssociatedBlockPage(destination,id,nullptr))page->adoptContent(content);
        else delete content;
    }else{
        MyCodeEditor* view=nullptr;for(auto* candidate:destination->views())if(!isAuxiliaryView(candidate)){view=candidate;break;}
        if(!view)view=createView(destination,activeTabWidget());
        if(view)view->adoptAssociatedBlockContent(id,content);else delete content;
    }
}

QList<AssociatedBlockPage*> TabManager::associatedBlockPages() const {
    QList<AssociatedBlockPage*> result;
    if (splitController) for (auto* group : splitController->groups())
        for (int i=0; i<group->count(); ++i)
            if (auto* page=qobject_cast<AssociatedBlockPage*>(group->widget(i))) result.append(page);
    return result;
}
AssociatedBlockPage* TabManager::currentAssociatedBlock() const {
    auto* group=activeTabWidget(); return group ? qobject_cast<AssociatedBlockPage*>(group->currentWidget()) : nullptr;
}
QList<AssociatedBlockPage*> TabManager::associatedBlockPresentations() const {
    auto result=associatedBlockPages();for(auto page:blockPresentations)if(page)result.append(page);return result;
}
bool TabManager::blockDragInProgress() const { return blockDragController && blockDragController->dragInProgress(); }
EditorBlockContent* TabManager::takeBlockContent(SharedDocument* document,const QString& id) {
    if(!document)return nullptr;
    for(auto* page:associatedBlockPresentations())if(page->ownerDocument()==document && page->blockId()==id)
        if(auto* content=page->takeContent())return content;
    for(auto* editor:document->views())if(auto* content=editor->takeAssociatedBlockContent(id))return content;
    return nullptr;
}
AssociatedBlockPage* TabManager::createBlockPresentation(SharedDocument* document,const QString& id,QWidget* parent) {
    const auto* block=document ? DocumentBlockModel::get(document->textDocument())->find(id) : nullptr;
    if(!block || block->detached || block->removedReference())return nullptr;
    auto* page=new AssociatedBlockPage(document,id,parent,{},nullptr,takeBlockContent(document,id));
    page->setProperty("expandedBlockPresentation",true);
    blockPresentations.removeIf([](auto value){return value.isNull();});blockPresentations.append(page);
    connectBlockPresentation(page);return page;
}
void TabManager::connectBlockPresentation(AssociatedBlockPage* page) {
    connect(page,&AssociatedBlockPage::statusChanged,this,[this,page] { updateAssociatedBlockTitle(page); });
    connect(page,&AssociatedBlockPage::saveRequested,this,[this,page](bool saveAs) { saveSharedDocument(page->ownerDocument(),saveAs); });
    connect(page,&AssociatedBlockPage::clipboardRequested,this,[this,page](bool cut) {
        if(blockDragController)blockDragController->copyBlock(page->ownerDocument(),page->blockId(),cut);
    });
    connect(page,&AssociatedBlockPage::sourceRequested,this,[this,page] {
        auto* owner=page->ownerDocument();if(!owner)return;
        for(auto* view:owner->views())if(!isAuxiliaryView(view)){splitController->activatePage(view);return;}
        createView(owner,splitController->groupForPage(page));
    });
}
AssociatedBlockPage* TabManager::openAssociatedBlock(SharedDocument* document, const QString& id, QTabWidget* group, const QString& viewId) {
    if (!document || !splitController || sharedDocuments->documentById(document->documentId()) != document) return nullptr;
    auto* model=DocumentBlockModel::get(document->textDocument());
    const auto* block=model->find(id); if (!block || !block->detached) return nullptr;
    return createAssociatedBlockPage(document,id,group,viewId);
}
AssociatedBlockPage* TabManager::createAssociatedBlockPage(SharedDocument* document, const QString& id, QTabWidget* group, const QString& viewId, const EditorAssociatedBlock* prepared) {
    for (auto* page : associatedBlockPages()) if (page->ownerDocument()==document && page->blockId()==id) {
        splitController->activatePage(page); return page;
    }
    if (!group) group=activeTabWidget(); if (!group) return nullptr;
    auto* page=new AssociatedBlockPage(document,id,group,viewId,prepared);
    if (!splitController->registerPage(page,{},"associatedBlock",[this,page] { return closeAssociatedBlock(page); })) { delete page; return nullptr; }
    if (document->fileName().isEmpty() && document->property("associatedWorkspace").toString().isEmpty()) document->setProperty("associatedWorkspace",activeWorkspaceRoot);
    connect(page,&AssociatedBlockPage::statusChanged,this,[this,page] { updateAssociatedBlockTitle(page); });
    connect(page,&AssociatedBlockPage::saveRequested,this,[this,page](bool saveAs) { saveSharedDocument(page->ownerDocument(),saveAs); });
    connect(page,&AssociatedBlockPage::clipboardRequested,this,[this,page](bool cut) {
        if (blockDragController) blockDragController->copyBlock(page->ownerDocument(),page->blockId(),cut);
    });
    connect(page,&AssociatedBlockPage::sourceRequested,this,[this,page] {
        auto* owner=page->ownerDocument(); if (!owner) return;
        for (auto* view : owner->views()) if (!isAuxiliaryView(view)) { splitController->activatePage(view); return; }
        createView(owner,splitController->groupForPage(page));
    });
    const auto* iconBlock = prepared ? prepared : DocumentBlockModel::get(document->textDocument())->find(id);
    group->addTab(page,RoundedIcons::icon(iconBlock ? RoundedIcons::blockKind(iconBlock->type) : RoundedIcons::BlockText),page->title());
    updateAssociatedBlockTitle(page); group->setCurrentWidget(page); splitController->setActiveGroup(group);
    applyWorkspaceScope(); emit workspaceSessionStateChanged(); return page;
}
AssociatedBlockPage* TabManager::detachAssociatedBlock(SharedDocument* document, const QString& id, QTabWidget* group,
    std::function<bool(AssociatedBlockPage*)> place, bool copy) {
    if (!document || document->readOnly()) return nullptr;
    auto* model=DocumentBlockModel::get(document->textDocument());
    QString issue;
    if (!model->flushContents(&issue) || !DocumentBlockTransfer::validateParticipant(document,true,&issue)) { model->setError(issue); return nullptr; }
    model->endTextSession();
    const auto* block=model->find(id); if (!block) return nullptr;
    const auto nextBlock=*block;
    const auto revision=model->revision();
    if (copy) {
        auto clone=nextBlock; clone.detached=true;
        QTextCursor anchor(document->textDocument()); anchor.setPosition(qBound(0,clone.start,document->textDocument()->characterCount()-1));
        QPointer<AssociatedBlockPage> page;
        const auto created=model->insert(clone,anchor,[&](const QString& cloneId) {
            clone.id=cloneId; page=createAssociatedBlockPage(document,cloneId,group,{},&clone);
            return page && (!place || place(page)) && !document->readOnly() && model->revision()==revision;
        });
        if (created.isEmpty()) { if (page) closeAssociatedBlock(page,false,false); return nullptr; }
        return page;
    }
    bool existing=false;
    QPointer<QTabWidget> originalGroup;
    ClosedTabState originalLocation; int originalIndex=-1;
    for (auto* page:associatedBlockPages()) if (page->ownerDocument()==document && page->blockId()==id) {
        existing=true; originalGroup=splitController->groupForPage(page);
        if(originalGroup) {
            originalIndex=originalGroup->indexOf(page); rememberClosedGroup(originalLocation,originalGroup);
            if(originalGroup->window()!=splitController->initialGroup()->window()) originalLocation.floatingGeometry=originalGroup->window()->geometry();
        }
    }
    auto* page=createAssociatedBlockPage(document,id,group);
    if (!page) return nullptr;
    if ((place && !place(page)) || document->readOnly() || model->revision()!=revision) {
        if (!existing) closeAssociatedBlock(page,false,false);
        else {
            auto* restore=originalGroup ? originalGroup.data() : originalLocation.floatingGeometry.isValid()
                ? splitController->regionGroup(originalLocation.regionId) : closedTabGroup(originalLocation);
            if(restore) splitController->movePage(page,restore,originalIndex);
            if(!originalGroup && originalLocation.floatingGeometry.isValid()) {
                if(auto* floating=splitController->floatPage(page,originalLocation.floatingGeometry.topLeft())) floating->window()->setGeometry(originalLocation.floatingGeometry);
            }
        }
        return nullptr;
    }
    if (!nextBlock.detached) {
        model->endTextSession(); auto next=nextBlock; next.detached=true;
        if (!model->update(next)) { closeAssociatedBlock(page,false,false); return nullptr; }
        for(auto presentation:blockPresentations)if(presentation && presentation->ownerDocument()==document && presentation->blockId()==id)
            if(auto* retained=presentation->takeContent()){page->adoptContent(retained);return page;}
        for(auto* view:document->views())if(auto* retained=view->takeAssociatedBlockContent(id)){page->adoptContent(retained);break;}
    }
    return page;
}
void TabManager::updateAssociatedBlockTitle(AssociatedBlockPage* page) {
    auto* document=page ? page->ownerDocument() : nullptr;
    auto* group=page ? splitController->groupForPage(page) : nullptr;
    if (!document || !group) return;
    const int index=group->indexOf(page); if (index<0) return;
    const QString title=page->title(); group->setTabText(index,(document->dirty() ? "* " : "")+title);
    UiControls::TabPresentation state;
    state.documentId=document->documentId(); state.viewId=page->viewIdentity(); state.title=title;
    state.path=QDir::toNativeSeparators(document->fileName()); state.dirty=document->dirty(); state.readOnly=document->readOnly();
    state.conflict=document->externalState()==SharedDocumentExternalState::Conflict;
    state.externalChange=document->externalState()==SharedDocumentExternalState::ExternallyModified;
    UiControls::setTabPresentation(group,page,state);
    if (contextPage()==page) emit contextDocumentChanged(contextDocumentMetadata());
}
bool TabManager::closeAssociatedBlock(AssociatedBlockPage* page, bool confirm, bool remember) {
    if (!page || !page->ownerDocument()) return false;
    QPointer<SharedDocument> document=page->ownerDocument();
    auto* group=splitController->groupForPage(page); if (!group) return false;
    if(!page->finishEditing())return false;
    if (confirm && document->viewCount()==0 && document->consumerCount()==1 && !confirmCloseDocument(document)) return false;
    if (!document) return false;
    ClosedTabState location; rememberClosedGroup(location,group); location.viewState.viewId=page->viewIdentity();
    if (group->window()!=splitController->initialGroup()->window()) location.floatingGeometry=group->window()->geometry();
    blockPageLocations[document][page->blockId()]=location;
    if (remember) {
        ClosedTabState closed; closed.ownerDocumentId=document->documentId(); closed.associatedBlockId=page->blockId();
        closed.fileName=document->fileName(); closed.text=document->textDocument()->toPlainText(); rememberClosedGroup(closed,group);
        closed.floatingGeometry=location.floatingGeometry; closed.viewState.viewId=page->viewIdentity();
        if (closed.fileName.isEmpty()) {
            QString issue; closed.associations=DocumentBlockStore::get(page->model())->recoveryState(&issue,crashRecoveryService->recoveryRootPath());
            if (!issue.isEmpty()) { emit crashRecoveryOperationFailed(document->documentId(),issue); return false; }
        }
        recentlyClosedTabs.append(closed); while(recentlyClosedTabs.size()>32) recentlyClosedTabs.removeFirst();
    }
    splitController->unregisterPage(page); group->removeTab(group->indexOf(page));
    document->releaseConsumer(page); page->hide(); page->deleteLater(); repairContextPage();
    if (document->viewCount()==0 && document->consumerCount()==0) {
        clearRecoverySnapshot(document,false); sharedDocuments->releaseIfUnused(document);
    }
    if (!closingBatch) splitController->removeEmptyGroups();
    emit workspaceSessionStateChanged(); return true;
}
void TabManager::reconcileAssociatedBlocks(SharedDocument* document) {
    if (recoveryClosing || !document || !observedDocuments.contains(document)
        || (blockDragController && blockDragController->dragInProgress())) return;
    auto* model=DocumentBlockModel::get(document->textDocument()); QSet<QString> detached;
    for (const auto& block : model->items()) if (block.detached) detached.insert(block.id);
    const auto previous=previousDetachedBlocks.value(document); previousDetachedBlocks[document]=detached;
    for (const auto& id : detached-previous) {
        bool alreadyOpen=false;
        for (auto* page:associatedBlockPages()) if (page->ownerDocument()==document && page->blockId()==id) alreadyOpen=true;
        if (alreadyOpen) continue;
        const auto location=blockPageLocations.value(document).value(id);
        auto* group=location.groupId.isEmpty() ? activeTabWidget() : location.floatingGeometry.isValid()
            ? splitController->regionGroup(location.regionId) : closedTabGroup(location);
        if (auto* page=openAssociatedBlock(document,id,group,location.viewState.viewId); page && location.floatingGeometry.isValid()) {
            if (auto* floating=splitController->floatPage(page,location.floatingGeometry.topLeft())) floating->window()->setGeometry(location.floatingGeometry);
        }
    }
    for (auto* page : associatedBlockPages()) {
        if (page->ownerDocument()!=document || detached.contains(page->blockId())) continue;
        const auto* block=model->find(page->blockId());
        if (!block && document->viewCount()==0) continue; // Keep Undo reachable for the last visible consumer.
        if (document->viewCount()==0) createView(document,splitController->groupForPage(page));
        if(block)for(auto* view:document->views()) {
            view->adoptAssociatedBlockContent(page->blockId(),page->takeContent());break;
        }
        closeAssociatedBlock(page,false,false);
    }
}
