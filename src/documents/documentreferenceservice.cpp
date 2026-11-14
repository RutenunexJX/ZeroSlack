#include "documentreferenceservice.h"
#include "documentblockstore.h"
#include "pinloomreferencepicker.h"
#include <QCryptographicHash>
#include <QMenu>
#include <QTextDocument>
#include <QUuid>
#include <algorithm>

namespace {
QString hashTarget(const QUrl& uri) {
    return QString::fromLatin1(QCryptographicHash::hash(uri.toEncoded(),QCryptographicHash::Sha256).toHex());
}
PinloomCodeLinkAnchorRecord semanticAnchor(const EditorAssociatedBlock& block) {
    PinloomCodeLinkAnchorRecord anchor; anchor.id=block.id;
    anchor.source=PinloomSourceSelection::fromVariantMap(block.reference.value("source").toMap());
    PinloomCodeLinkRecord target; target.id=block.id; target.uri=QUrl(block.uri);
    target.title=block.caption; target.source=anchor.source; anchor.links.append(target);
    return anchor;
}
QString anchorStatus(const ResolvedPinloomCodeLink& anchor) {
    if (anchor.available()) return {};
    return anchor.resolution==PinloomCodeLinkResolution::Ambiguous
        ? QStringLiteral("Ambiguous source anchor · Binding retained")
        : QStringLiteral("Missing source anchor · Binding retained");
}
}
DocumentReferenceService::Selection DocumentReferenceService::pick(PinloomHostClient* client, QWidget* parent, const QString& initialUri) {
    PinloomReferencePicker picker(client,initialUri,parent);
    if (picker.exec()!=QDialog::Accepted || picker.selectedUri().isEmpty()) return {};
    return {picker.selectedUri(),picker.selectedCaption(),true};
}
QString DocumentReferenceService::insertSemantic(DocumentBlockModel* model, const PinloomSourceSelection& source,
    const QString& text, const Selection& target, QString* error) {
    if (!model || !source.isValid() || !target.accepted || target.uri.isEmpty() || !model->flushContents(error)) return {};
    auto* document=qobject_cast<QTextDocument*>(model->parent());
    if (!document || document->toPlainText()!=text) { if(error)*error="The source document changed. Select the code object again."; return {}; }
    EditorAssociatedBlock block; block.type="reference"; block.uri=target.uri; block.caption=target.caption;
    block.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    block.reference={{"version",1},{"sourceKind","semantic"},{"display","compact"},{"source",source.toVariantMap()}};
    const auto resolved=PinloomCodeLinkStore::resolveAnchors({semanticAnchor(block)},source.workspaceRoot,source.absoluteFilePath,text);
    if (resolved.size()!=1 || !resolved.first().available()) { if(error)*error="The code object is missing or ambiguous. Select it again."; return {}; }
    QTextCursor cursor(document); cursor.setPosition(resolved.first().startPosition);
    cursor.setPosition(resolved.first().endPosition,QTextCursor::KeepAnchor);
    return model->insert(block,cursor);
}
bool DocumentReferenceService::hasLegacy(const DocumentBlockModel* model,const QString& anchorId,const QString& linkId) {
    if (!model) return false;
    for (const auto& block:model->items()) if (block.reference.value("sourceKind")=="legacy"
        && block.reference.value("anchorId")==anchorId && block.reference.value("linkId")==linkId) return true;
    return false;
}
QString DocumentReferenceService::convertLegacy(DocumentBlockModel* model,const ResolvedPinloomCodeLink& anchor,
    const QString& linkId,const QString& display,QString* error) {
    if (!model || !QStringList{"compact","embedded","removed"}.contains(display) || !model->flushContents(error)) return {};
    for (const auto& block:model->items()) if (block.reference.value("sourceKind")=="legacy"
        && block.reference.value("anchorId")==anchor.anchor.id && block.reference.value("linkId")==linkId) {
        const auto id=block.id; return setDisplay(model,id,display) ? id : QString();
    }
    const auto found=std::find_if(anchor.anchor.links.cbegin(),anchor.anchor.links.cend(),[&](const auto& link){return link.id==linkId;});
    if (found==anchor.anchor.links.cend()) { if(error)*error="The original relation is no longer available."; return {}; }
    auto* document=qobject_cast<QTextDocument*>(model->parent()); if(!document)return {};
    EditorAssociatedBlock block; block.type="reference";
    block.reference={{"version",1},{"sourceKind","legacy"},{"display",display},
        {"anchorId",anchor.anchor.id},{"linkId",linkId},{"targetHash",hashTarget(found->uri)}};
    QTextCursor cursor(document);
    cursor.setPosition(qBound(0,anchor.available()?anchor.startPosition:anchor.anchor.source.startPosition,document->characterCount()-1));
    if(anchor.available())cursor.setPosition(qBound(cursor.position(),anchor.endPosition,document->characterCount()-1),QTextCursor::KeepAnchor);
    return model->insert(block,cursor);
}
bool DocumentReferenceService::setDisplay(DocumentBlockModel* model,const QString& id,const QString& display) {
    if (!model || !QStringList{"compact","embedded","removed"}.contains(display) || !model->flushContents()) return false;
    const auto* current=model->find(id); if(!current || current->type!="reference")return false;
    if(display=="removed")return model->remove(id);
    if(current->reference.value("display","embedded")==display)return true;
    auto next=*current; if(next.reference.isEmpty())next.reference=DocumentBlockModel::positionReference();
    next.reference["display"]=display; return model->update(next);
}
void DocumentReferenceService::refresh(DocumentBlockModel* model,const QString& file,const QString& text,
    const TSDocument* syntax,const PinloomCodeLinkStore* legacy) {
    if(!model)return;
    QList<PinloomCodeLinkAnchorRecord> semantic;
    for(const auto& block:model->items())if(block.reference.value("sourceKind")=="semantic")semantic.append(semanticAnchor(block));
    const auto root=DocumentBlockStore::get(model)->rootPath();
    const auto resolved=PinloomCodeLinkStore::resolveAnchors(semantic,root,file,text,syntax);
    const auto old=legacy?legacy->linksForDocument(file,text,syntax):QList<ResolvedPinloomCodeLink>{};
    QHash<QString,ReferencePresentation> views;
    for(const auto& block:model->items()) {
        const auto kind=block.reference.value("sourceKind").toString();
        if(kind!="legacy" && kind!="semantic")continue;
        ReferencePresentation value{block.uri,block.caption,"Missing source anchor · Binding retained",block.start,block.end,true};
        const auto& candidates=kind=="legacy"?old:resolved;
        const auto wanted=kind=="legacy"?block.reference.value("anchorId").toString():block.id;
        for(const auto& anchor:candidates)if(anchor.anchor.id==wanted) {
            value.status=anchorStatus(anchor);value.missing=!anchor.available();
            if(anchor.available()){value.start=anchor.startPosition;value.end=anchor.endPosition;}
            if(kind=="legacy") {
                value.status="The original relation is unavailable · Binding retained";
                for(const auto& link:anchor.anchor.links)if(link.id==block.reference.value("linkId")) {
                    if(hashTarget(link.uri)!=block.reference.value("targetHash"))value.status="The original relation changed · Binding retained";
                    else {value.uri=link.uri.toString();value.caption=link.title;value.status=anchorStatus(anchor);}
                    break;
                }
            }
            break;
        }
        if(kind=="legacy" && (!legacy || !legacy->loadFailureReason().isEmpty()))value.status=legacy?legacy->loadFailureReason():"Original relation store unavailable";
        views.insert(block.id,value);
    }
    model->setReferencePresentations(views);
}
void DocumentReferenceService::addMenu(QMenu* menu,DocumentBlockModel* value,const QString& id,
    QWidget* host,std::function<bool()> readOnly) {
    const auto* block=value?value->find(id):nullptr;if(!block || block->type!="reference")return;
    const QPointer<DocumentBlockModel> model(value);
    for(const auto& item:QList<QPair<QString,QString>>{{"Compact link","compact"},{"Embedded preview","embedded"}}) {
        auto* action=menu->addAction(item.first);action->setCheckable(true);
        action->setObjectName("referenceDisplay_"+item.second);
        QObject::connect(menu,&QMenu::aboutToShow,host,[model,id,action,item,readOnly] {
            const auto* current=model?model->find(id):nullptr;action->setEnabled(current && !readOnly());
            action->setChecked(current && current->reference.value("display","embedded")==item.second);
        });
        QObject::connect(action,&QAction::triggered,host,[model,id,item,readOnly]{if(model && !readOnly())setDisplay(model,id,item.second);});
    }
    auto* retry=menu->addAction("Retry reference");
    QObject::connect(retry,&QAction::triggered,host,[model,id]{if(model)emit model->referenceRetryRequested(id);});
}
