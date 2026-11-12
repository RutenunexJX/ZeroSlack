#include "tabmanager.h"
#include "associatedblockpage.h"
#include "associatedblockdragcontroller.h"
#include "documentblockmodel.h"
#include "documentblockstore.h"
#include "uicontrols.h"
#include "roundedicons.h"
#include <QTextDocument>
#include <QDir>

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
AssociatedBlockPage* TabManager::openAssociatedBlock(SharedDocument* document, const QString& id, QTabWidget* group, const QString& viewId) {
    if (!document || !splitController || sharedDocuments->documentById(document->documentId()) != document) return nullptr;
    auto* model=DocumentBlockModel::get(document->textDocument());
    const auto* block=model->find(id); if (!block || !block->detached) return nullptr;
    return createAssociatedBlockPage(document,id,group,viewId);
}
AssociatedBlockPage* TabManager::createAssociatedBlockPage(SharedDocument* document, const QString& id, QTabWidget* group, const QString& viewId) {
    for (auto* page : associatedBlockPages()) if (page->ownerDocument()==document && page->blockId()==id) {
        splitController->activatePage(page); return page;
    }
    if (!group) group=activeTabWidget(); if (!group) return nullptr;
    auto* page=new AssociatedBlockPage(document,id,group,viewId);
    if (!splitController->registerPage(page,{},"associatedBlock",[this,page] { return closeAssociatedBlock(page); })) { delete page; return nullptr; }
    if (document->fileName().isEmpty() && document->property("associatedWorkspace").toString().isEmpty()) document->setProperty("associatedWorkspace",activeWorkspaceRoot);
    connect(page,&AssociatedBlockPage::statusChanged,this,[this,page] { updateAssociatedBlockTitle(page); });
    connect(page,&AssociatedBlockPage::saveRequested,this,[this,page](bool saveAs) { saveSharedDocument(page->ownerDocument(),saveAs); });
    connect(page,&AssociatedBlockPage::sourceRequested,this,[this,page] {
        auto* owner=page->ownerDocument(); if (!owner) return;
        for (auto* view : owner->views()) if (!isAuxiliaryView(view)) { splitController->activatePage(view); return; }
        createView(owner,splitController->groupForPage(page));
    });
    group->addTab(page,RoundedIcons::icon(RoundedIcons::File),page->title());
    updateAssociatedBlockTitle(page); group->setCurrentWidget(page); splitController->setActiveGroup(group);
    applyWorkspaceScope(); emit workspaceSessionStateChanged(); return page;
}
AssociatedBlockPage* TabManager::detachAssociatedBlock(SharedDocument* document, const QString& id, QTabWidget* group,
    std::function<bool(AssociatedBlockPage*)> place) {
    if (!document || document->readOnly()) return nullptr;
    auto* model=DocumentBlockModel::get(document->textDocument());
    const auto* block=model->find(id); if (!block) return nullptr;
    const auto nextBlock=*block;
    const auto revision=model->revision();
    bool existing=false;
    for (auto* page:associatedBlockPages()) if (page->ownerDocument()==document && page->blockId()==id) existing=true;
    auto* page=createAssociatedBlockPage(document,id,group);
    if (!page) return nullptr;
    if ((place && !place(page)) || document->readOnly() || model->revision()!=revision) {
        if (!existing) closeAssociatedBlock(page,false,false);
        return nullptr;
    }
    if (!nextBlock.detached) {
        model->endTextSession(); auto next=nextBlock; next.detached=true;
        if (!model->update(next)) { closeAssociatedBlock(page,false,false); return nullptr; }
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
    page->finishEditing();
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
        closeAssociatedBlock(page,false,false);
    }
}
