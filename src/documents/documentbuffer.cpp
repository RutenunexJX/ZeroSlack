#include "documentbuffer.h"
#include "editorfileidentity.h"
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <atomic>
#include <algorithm>

namespace { std::atomic_uint64_t nextDocumentSerial{0}; }

DocumentBuffer::DocumentBuffer(const QString& documentId, const QString& fileName,
    const QString& initialText, QObject* parent, QTextDocument* ownedDocument,
    const DocumentFileReadResult* initialFile)
    : QObject(parent), serial(++nextDocumentSerial), id(documentId.isEmpty() ? QStringLiteral("untitled:")
        + QUuid::createUuid().toString(QUuid::WithoutBraces) : documentId),
      normalizedFileName(EditorFileIdentity::normalized(fileName)),
      document(ownedDocument ? ownedDocument : new QTextDocument)
{
    document->setParent(this);
    document->setUndoRedoEnabled(true);
    resetText(initialText, 0, true, initialFile);
    connect(document, &QTextDocument::contentsChange, this,
            [this](int, int, int) { handleContentsChange(); });
    // Qt omits contentsChange until a layout is attached. Only use the broad
    // notification in that case: a highlighter in an editor also emits it for
    // formatting, which must not advance the shared text revision.
    connect(document, &QTextDocument::contentsChanged, this, [this] {
        if (!document->findChild<QAbstractTextDocumentLayout*>(QString(), Qt::FindDirectChildrenOnly))
            handleContentsChange();
    });
    connect(document,&QTextDocument::modificationChanged,this,[this](bool){publishDirtyIfChanged();});
}
void DocumentBuffer::setReadOnly(bool nextReadOnly)
{
    if (readOnlyState == nextReadOnly) return;
    readOnlyState = nextReadOnly;
    emit statusChanged();
}

QString DocumentBuffer::documentId() const
{
    return id;
}

QString DocumentBuffer::fileName() const
{
    return normalizedFileName;
}

QTextDocument* DocumentBuffer::textDocument() const
{
    return document;
}

std::uint64_t DocumentBuffer::textRevision() const
{
    return revision;
}

std::uint64_t DocumentBuffer::savedTextRevision() const
{
    return savedRevision;
}

QByteArray DocumentBuffer::savedBaselineSha256() const
{
    return savedBaselineDigest;
}

QDateTime DocumentBuffer::savedBaselineModifiedUtc() const
{
    return savedBaselineModifiedTimeUtc;
}

bool DocumentBuffer::dirty() const
{
    return document && document->isModified();
}

bool DocumentBuffer::readOnly() const
{
    return readOnlyState;
}

SharedDocumentExternalState DocumentBuffer::externalState() const
{
    return externalFileState;
}

void DocumentBuffer::resetText(const QString& text,
                               std::uint64_t nextRevision,
                               bool markClean,
                               const DocumentFileReadResult* source)
{
    if (!document)
        return;

    const bool previousDirty = dirty();
    loadingText = true;
    document->setUndoRedoEnabled(false);
    document->setPlainText(text);
    document->setUndoRedoEnabled(true);
    revision = textInitialized ? std::max(nextRevision, revision + 1) : nextRevision;
    textInitialized = true;
    if (markClean) {
        savedRevision = revision;
        document->setModified(false);
        if (source && source->available) {
            restoreSavedBaseline(*source);
        } else {
            captureSavedBaseline();
        }
    }
    loadingText = false;
    lastDirtyState = dirty();
    if (previousDirty != lastDirtyState)
        emit dirtyChanged(lastDirtyState);
    emit textRevisionChanged(revision);
}

void DocumentBuffer::restoreSavedBaseline(
    const QByteArray& sha256,
    const QDateTime& modifiedUtc)
{
    if (sha256.size() == 32)
        savedBaselineDigest = sha256;
    savedBaselineModifiedTimeUtc =
        modifiedUtc.isValid()
        ? modifiedUtc.toUTC()
        : QDateTime();
}

void DocumentBuffer::restoreSavedBaseline(const DocumentFileReadResult& source)
{
    if (!source.available)
        return;
    restoreSavedBaseline(source.rawSha256, source.modifiedUtc);
    format = source.format;
}

void DocumentBuffer::markSaved()
{
    if (!document)
        return;
    savedRevision = revision;
    document->setModified(false);
    captureSavedBaseline();
    publishDirtyIfChanged();
    setExternalState(SharedDocumentExternalState::Current);
    emit statusChanged();
}

void DocumentBuffer::markSaved(const QByteArray& sha256,
                               const QDateTime& modifiedUtc)
{
    if (!document)
        return;
    savedRevision = revision;
    document->setModified(false);
    restoreSavedBaseline(sha256, modifiedUtc);
    publishDirtyIfChanged();
    setExternalState(SharedDocumentExternalState::Current);
    emit statusChanged();
}

void DocumentBuffer::setExternalState(
    SharedDocumentExternalState state)
{
    if (externalFileState == state)
        return;
    externalFileState = state;
    emit statusChanged();
}

void DocumentBuffer::handleContentsChange()
{
    if (loadingText)
        return;
    ++revision;
    emit textRevisionChanged(revision);
    publishDirtyIfChanged();
}

void DocumentBuffer::publishDirtyIfChanged()
{
    if (loadingText)
        return;
    const bool currentDirty = dirty();
    if (lastDirtyState == currentDirty)
        return;
    lastDirtyState = currentDirty;
    emit dirtyChanged(currentDirty);
    emit statusChanged();
}

void DocumentBuffer::captureSavedBaseline()
{
    if (!document)
        return;
    if (normalizedFileName.isEmpty()) {
        savedBaselineDigest =
            QCryptographicHash::hash(
                document->toPlainText().toUtf8(),
                QCryptographicHash::Sha256);
        savedBaselineModifiedTimeUtc = QDateTime();
        return;
    }
    const auto source = readDocumentFile(normalizedFileName);
    if (source.available) {
        restoreSavedBaseline(source);
    } else {
        savedBaselineDigest = QCryptographicHash::hash(document->toPlainText().toUtf8(), QCryptographicHash::Sha256);
        savedBaselineModifiedTimeUtc = {};
    }
}
