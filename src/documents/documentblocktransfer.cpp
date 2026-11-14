#include "documentblocktransfer.h"
#include "documentblockmodel.h"
#include "documentblockstore.h"
#include "shareddocument.h"
#include <QJsonArray>
#include <QTextDocument>

bool DocumentBlockTransfer::validateParticipant(SharedDocument* document, bool write, QString* error) {
    if (!document || (write && document->readOnly())) {
        if (error) *error="A participating document is closed or read-only.";
        return false;
    }
    if (document->externalState()!=SharedDocumentExternalState::Current) {
        if (error) *error="Resolve the external document change before transferring blocks.";
        return false;
    }
    if (!document->fileName().isEmpty()) {
        if (!document->validateSourcePath(error)) return false;
        QString issue;
        if (DocumentBlockStore::fileRevision(document->fileName(),&issue)!=document->savedBaselineSha256() || !issue.isEmpty()) {
            if (error) *error="The source file changed externally. Both in-memory documents were preserved. "+issue;
            return false;
        }
    }
    return DocumentBlockStore::get(DocumentBlockModel::get(document->textDocument()))->validateStorage(error);
}
BlockTransferSource DocumentBlockTransfer::capture(SharedDocument* document, const QString& id) {
    if (!document) return {};
    auto* model=DocumentBlockModel::get(document->textDocument()); if (!model->flushContents()) return {}; model->endTextSession();
    return {document,document->documentId(),id,document->instanceSerial(),document->textRevision(),model->revision()};
}
bool DocumentBlockTransfer::current(const BlockTransferSource& source) {
    return source.document && source.document->documentId()==source.identity && source.document->instanceSerial()==source.instance
        && source.document->textRevision()==source.textRevision
        && DocumentBlockModel::get(source.document->textDocument())->revision()==source.associationRevision
        && DocumentBlockModel::get(source.document->textDocument())->find(source.blockId);
}
QString DocumentBlockTransfer::apply(const BlockTransferSource& source, SharedDocument* target,
    const QTextCursor& destination, Mode mode, const QString& beforeId, QString* error) {
    if (error) error->clear();
    const bool copy=mode==Mode::Copy;
    if (!current(source) || !target || destination.document()!=target->textDocument()) {
        if (error) *error="The block or insertion target changed. Start the operation again.";
        return {};
    }
    if (!validateParticipant(source.document,!copy,error) || !validateParticipant(target,true,error)) return {};
    auto* origin=DocumentBlockModel::get(source.document->textDocument());
    auto* receiving=DocumentBlockModel::get(target->textDocument());
    if (!copy && origin!=receiving && receiving->find(source.blockId)) {
        if (error) *error="The destination already owns this block identity.";
        return {};
    }
    if (!beforeId.isEmpty() && (!receiving->find(beforeId) || (!copy && origin==receiving && beforeId==source.blockId))) {
        if (error) *error="The insertion target is no longer available.";
        return {};
    }
    const auto value=*origin->find(source.blockId);
    if (!value.asset.isEmpty() && origin!=receiving) {
        const auto bytes=DocumentBlockStore::get(origin)->assetBytes(value.asset,error);
        if (bytes.isEmpty() || !DocumentBlockStore::get(receiving)->stageAsset(value.asset,bytes,error)) return {};
    }
    const QPointer<SharedDocument> left=source.document,right=target;
    const auto guard=[left,right](QString* issue) { return validateParticipant(left,true,issue) && validateParticipant(right,true,issue); };
    return origin->transferTo(receiving,source.blockId,destination,copy,beforeId,guard);
}
QJsonObject DocumentBlockTransfer::snapshot(SharedDocument* source, const QString& id, QString* error) {
    if (error) error->clear();
    if (!source) { if (error) *error="The source document is unavailable."; return {}; }
    auto* model=DocumentBlockModel::get(source->textDocument());
    if (!model->flushContents(error)) return {};
    model->endTextSession();
    const auto* block=model->find(id); if (!block) { if (error) *error="The selected block no longer exists."; return {}; }
    auto metadata=model->serialize();
    for (const auto& item:metadata.value("blocks").toArray()) if (item.toObject().value("id")==id) {
        auto portable=item.toObject();
        if (block->type=="reference") {
            const auto resolved=model->presentation(id);
            if (resolved.uri.isEmpty() || resolved.removedReference()) { if(error)*error="Resolve the reference before copying it."; return {}; }
            portable["uri"]=resolved.uri; portable["caption"]=resolved.caption;
            portable["reference"]=QJsonObject::fromVariantMap(DocumentBlockModel::positionReference(block->reference.value("display","embedded").toString()));
        }
        metadata["blocks"]=QJsonArray{portable}; break;
    }
    QJsonObject result{{"format","zeroslack.block-clipboard"},{"version",1},{"metadata",metadata}};
    if (!block->asset.isEmpty()) {
        const auto bytes=DocumentBlockStore::get(model)->assetBytes(block->asset,error);
        if (bytes.isEmpty()) return {};
        result["assetBase64"]=QString::fromLatin1(bytes.toBase64());
    }
    return result;
}
QString DocumentBlockTransfer::paste(const QJsonObject& snapshot, SharedDocument* target,
    const QTextCursor& destination, const QString& beforeId, QString* error) {
    if (error) error->clear();
    if (!target || destination.document()!=target->textDocument() || !validateParticipant(target,true,error)) return {};
    const auto metadata=snapshot.value("metadata").toObject();
    if (snapshot.value("format")!="zeroslack.block-clipboard" || snapshot.value("version").toInt()!=1
        || metadata.value("blocks").toArray().size()!=1) {
        if (error) *error="Unsupported block clipboard data."; return {};
    }
    QTextDocument validationDocument;
    auto* incoming=DocumentBlockModel::get(&validationDocument);
    if (!incoming->restore(metadata,error)) return {};
    const auto value=incoming->items().first();
    auto* receiving=DocumentBlockModel::get(target->textDocument());
    if (!beforeId.isEmpty() && !receiving->find(beforeId)) { if (error) *error="The paste insertion target changed."; return {}; }
    if (!value.asset.isEmpty()) {
        const auto encoded=snapshot.value("assetBase64").toString().toLatin1();
        if (encoded.size()>70*1024*1024) { if (error) *error="The block clipboard resource is too large."; return {}; }
        const auto bytes=QByteArray::fromBase64Encoding(encoded,QByteArray::AbortOnBase64DecodingErrors);
        if (!bytes) { if (error) *error="The clipboard resource encoding is invalid."; return {}; }
        if (!DocumentBlockStore::get(receiving)->stageAsset(value.asset,bytes.decoded,error)) return {};
    }
    return incoming->transferTo(receiving,value.id,destination,true,beforeId);
}
