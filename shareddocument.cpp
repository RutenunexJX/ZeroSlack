#include "shareddocument.h"

#include "editorfileidentity.h"
#include "mycodeeditor.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QPlainTextDocumentLayout>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTextCursor>
#include <QTextDocument>
#include <QUuid>

#include <algorithm>
#include <memory>
#include <vector>

namespace {
QString newUntitledDocumentId()
{
    return QStringLiteral("untitled:")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

int boundedDocumentPosition(QTextDocument* document, int position)
{
    return qBound(0,
                  position,
                  document
                      ? qMax(0, document->characterCount() - 1)
                      : 0);
}
}

SharedDocument::SharedDocument(const QString& documentId,
                               const QString& fileName,
                               const QString& initialText,
                               QObject* parent)
    : QObject(parent)
    , id(documentId.isEmpty() ? newUntitledDocumentId()
                              : documentId)
    , normalizedFileName(EditorFileIdentity::normalized(fileName))
    , document(new QTextDocument(this))
{
    document->setDocumentLayout(
        new QPlainTextDocumentLayout(document));
    document->setUndoRedoEnabled(true);
    resetText(initialText);
    connect(document,
            &QTextDocument::contentsChange,
            this,
            [this](int, int, int) {
                handleContentsChange();
            });
    connect(document,
            &QTextDocument::modificationChanged,
            this,
            [this](bool) {
                publishDirtyIfChanged();
            });
}

SharedDocument::~SharedDocument()
{
    const QList<MyCodeEditor*> attachedViews = views();
    for (MyCodeEditor* editor : attachedViews)
        detachView(editor, true);
}

QString SharedDocument::documentId() const
{
    return id;
}

QString SharedDocument::fileName() const
{
    return normalizedFileName;
}

QTextDocument* SharedDocument::textDocument() const
{
    return document;
}

std::uint64_t SharedDocument::textRevision() const
{
    return revision;
}

std::uint64_t SharedDocument::savedTextRevision() const
{
    return savedRevision;
}

QByteArray SharedDocument::savedBaselineSha256() const
{
    return savedBaselineDigest;
}

QDateTime SharedDocument::savedBaselineModifiedUtc() const
{
    return savedBaselineModifiedTimeUtc;
}

bool SharedDocument::dirty() const
{
    return document && document->isModified();
}

bool SharedDocument::readOnly() const
{
    return readOnlyState;
}

SharedDocumentExternalState SharedDocument::externalState() const
{
    return externalFileState;
}

void SharedDocument::resetText(const QString& text,
                               std::uint64_t nextRevision,
                               bool markClean)
{
    if (!document)
        return;

    const bool previousDirty = dirty();
    loadingText = true;
    document->setUndoRedoEnabled(false);
    document->setPlainText(text);
    document->setUndoRedoEnabled(true);
    revision = nextRevision;
    if (markClean) {
        savedRevision = revision;
        document->setModified(false);
        captureSavedBaseline();
    }
    loadingText = false;
    lastDirtyState = dirty();
    if (previousDirty != lastDirtyState)
        emit dirtyChanged(lastDirtyState);
    emit textRevisionChanged(revision);
}

bool SharedDocument::reloadCleanText(const QString& text)
{
    if (!document || dirty()
        || externalFileState
               == SharedDocumentExternalState::Conflict) {
        return false;
    }

    QHash<MyCodeEditor*, SharedDocumentViewState> states;
    std::vector<std::unique_ptr<QSignalBlocker>>
        viewSignalBlockers;
    viewSignalBlockers.reserve(
        static_cast<std::size_t>(viewCount()));
    for (MyCodeEditor* editor : views()) {
        states.insert(editor, viewState(editor));
        viewSignalBlockers.push_back(
            std::make_unique<QSignalBlocker>(editor));
    }

    if (document->toPlainText() != text)
        resetText(text, revision + 1, true);
    else {
        document->setModified(false);
        savedRevision = revision;
        captureSavedBaseline();
    }

    for (auto it = states.cbegin(); it != states.cend(); ++it) {
        if (it.key())
            applyViewState(it.key(), it.value());
    }
    viewSignalBlockers.clear();
    for (MyCodeEditor* editor : views())
        captureViewState(editor);
    setExternalState(SharedDocumentExternalState::Current);
    return true;
}

bool SharedDocument::acceptExternalText(const QString& text)
{
    if (!document)
        return false;

    QHash<MyCodeEditor*, SharedDocumentViewState> states;
    std::vector<std::unique_ptr<QSignalBlocker>>
        viewSignalBlockers;
    viewSignalBlockers.reserve(
        static_cast<std::size_t>(viewCount()));
    for (MyCodeEditor* editor : views()) {
        states.insert(editor, viewState(editor));
        viewSignalBlockers.push_back(
            std::make_unique<QSignalBlocker>(editor));
    }

    const bool wasDirty = dirty();
    const bool textChanged =
        document->toPlainText() != text;
    loadingText = true;
    if (textChanged) {
        QTextCursor replacement(document);
        replacement.beginEditBlock();
        replacement.select(QTextCursor::Document);
        replacement.insertText(text);
        replacement.endEditBlock();
        ++revision;
    }

    savedRevision = revision;
    document->setModified(false);
    captureSavedBaseline();
    loadingText = false;
    lastDirtyState = false;
    if (wasDirty)
        emit dirtyChanged(false);
    if (textChanged)
        emit textRevisionChanged(revision);

    for (auto it = states.cbegin(); it != states.cend(); ++it) {
        if (it.key())
            applyViewState(it.key(), it.value());
    }
    viewSignalBlockers.clear();
    for (MyCodeEditor* editor : views())
        captureViewState(editor);

    setExternalState(SharedDocumentExternalState::Current);
    emit statusChanged();
    return true;
}

bool SharedDocument::restoreUnsavedText(
    const QString& text,
    std::uint64_t recoveredRevision)
{
    if (!document)
        return false;

    QHash<MyCodeEditor*, SharedDocumentViewState> states;
    for (MyCodeEditor* editor : views())
        states.insert(editor, viewState(editor));

    const bool wasDirty = dirty();
    loadingText = true;
    if (document->toPlainText() != text) {
        QTextCursor replacement(document);
        replacement.beginEditBlock();
        replacement.select(QTextCursor::Document);
        replacement.insertText(text);
        replacement.endEditBlock();
    }
    revision = std::max(
        revision + 1,
        recoveredRevision);
    document->setModified(true);
    loadingText = false;
    lastDirtyState = true;

    std::vector<std::unique_ptr<QSignalBlocker>>
        viewSignalBlockers;
    viewSignalBlockers.reserve(
        static_cast<std::size_t>(viewCount()));
    for (MyCodeEditor* editor : views()) {
        viewSignalBlockers.push_back(
            std::make_unique<QSignalBlocker>(editor));
    }
    for (auto it = states.cbegin(); it != states.cend(); ++it) {
        if (it.key()) {
            it.key()->attachSharedDocument(
                document,
                revision);
            applyViewState(it.key(), it.value());
        }
    }
    viewSignalBlockers.clear();
    for (MyCodeEditor* editor : views())
        captureViewState(editor);

    if (!wasDirty)
        emit dirtyChanged(true);
    emit textRevisionChanged(revision);
    emit statusChanged();
    return true;
}

void SharedDocument::restoreSavedBaseline(
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

void SharedDocument::markSaved()
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

void SharedDocument::setReadOnly(bool nextReadOnly)
{
    if (readOnlyState == nextReadOnly)
        return;
    readOnlyState = nextReadOnly;
    for (MyCodeEditor* editor : views()) {
        if (editor)
            editor->setReadOnly(readOnlyState);
    }
    emit statusChanged();
}

void SharedDocument::setExternalState(
    SharedDocumentExternalState state)
{
    if (externalFileState == state)
        return;
    externalFileState = state;
    emit statusChanged();
}

QString SharedDocument::attachView(
    MyCodeEditor* editor,
    const SharedDocumentViewState& restoredState)
{
    if (!editor || !document)
        return QString();
    const auto existing = viewBindings.constFind(editor);
    if (existing != viewBindings.constEnd())
        return existing->state.viewId;

    ViewBinding binding;
    binding.editor = editor;
    binding.state = restoredState;
    const auto viewIdInUse =
        [this](const QString& candidate) {
            if (candidate.isEmpty())
                return false;
            for (auto it = viewBindings.cbegin();
                 it != viewBindings.cend();
                 ++it) {
                if (it->state.viewId == candidate)
                    return true;
            }
            return false;
        };
    if (binding.state.viewId.isEmpty()
        || viewIdInUse(binding.state.viewId)) {
        do {
            binding.state.viewId =
                QUuid::createUuid().toString(
                    QUuid::WithoutBraces);
        } while (viewIdInUse(binding.state.viewId));
    }

    editor->attachSharedDocument(document, revision);
    if (!normalizedFileName.isEmpty())
        editor->setDocumentFileName(normalizedFileName);
    editor->setReadOnly(readOnlyState);
    applyViewState(editor, binding.state);

    binding.connections.append(connect(
        editor,
        &QPlainTextEdit::cursorPositionChanged,
        this,
        [this, editor]() {
            captureViewState(editor);
        }));
    if (QScrollBar* bar = editor->verticalScrollBar()) {
        binding.connections.append(connect(
            bar,
            &QScrollBar::valueChanged,
            this,
            [this, editor](int) {
                captureViewState(editor);
            }));
    }
    if (QScrollBar* bar = editor->horizontalScrollBar()) {
        binding.connections.append(connect(
            bar,
            &QScrollBar::valueChanged,
            this,
            [this, editor](int) {
                captureViewState(editor);
            }));
    }
    binding.connections.append(connect(
        editor,
        &QObject::destroyed,
        this,
        [this, editor]() {
            detachView(editor, false);
        }));

    const QString viewId = binding.state.viewId;
    viewBindings.insert(editor, std::move(binding));
    captureViewState(editor);
    emit viewAttached(viewId);
    return viewId;
}

bool SharedDocument::detachView(
    MyCodeEditor* editor,
    bool preserveIndependentCopy)
{
    auto found = viewBindings.find(editor);
    if (found == viewBindings.end())
        return false;

    if (preserveIndependentCopy && editor)
        captureViewState(editor);
    const SharedDocumentViewState state = found->state;
    disconnectViewBinding(&found.value());
    viewBindings.erase(found);

    if (preserveIndependentCopy && editor && document) {
        auto* independentDocument = new QTextDocument(editor);
        independentDocument->setDocumentLayout(
            new QPlainTextDocumentLayout(independentDocument));
        independentDocument->setUndoRedoEnabled(false);
        independentDocument->setPlainText(document->toPlainText());
        independentDocument->setUndoRedoEnabled(true);
        independentDocument->setModified(document->isModified());
        editor->attachSharedDocument(independentDocument, revision);
        applyViewState(editor, state);
    }
    emit viewDetached(state.viewId);
    return true;
}

int SharedDocument::viewCount() const
{
    return viewBindings.size();
}

QList<MyCodeEditor*> SharedDocument::views() const
{
    QList<MyCodeEditor*> result;
    result.reserve(viewBindings.size());
    for (const ViewBinding& binding : viewBindings) {
        if (binding.editor)
            result.append(binding.editor.data());
    }
    return result;
}

SharedDocumentViewState SharedDocument::viewState(
    MyCodeEditor* editor) const
{
    const auto found = viewBindings.constFind(editor);
    if (found == viewBindings.constEnd())
        return {};
    SharedDocumentViewState result = found->state;
    if (!editor)
        return result;
    const QTextCursor cursor = editor->textCursor();
    result.cursorPosition = cursor.position();
    result.anchorPosition = cursor.anchor();
    if (const QScrollBar* bar = editor->verticalScrollBar())
        result.verticalScrollValue = bar->value();
    if (const QScrollBar* bar = editor->horizontalScrollBar())
        result.horizontalScrollValue = bar->value();
    return result;
}

void SharedDocument::captureViewState(MyCodeEditor* editor)
{
    auto found = viewBindings.find(editor);
    if (found == viewBindings.end() || !editor)
        return;
    const QTextCursor cursor = editor->textCursor();
    found->state.cursorPosition = cursor.position();
    found->state.anchorPosition = cursor.anchor();
    if (const QScrollBar* bar = editor->verticalScrollBar())
        found->state.verticalScrollValue = bar->value();
    if (const QScrollBar* bar = editor->horizontalScrollBar())
        found->state.horizontalScrollValue = bar->value();
}

void SharedDocument::handleContentsChange()
{
    if (loadingText)
        return;
    ++revision;
    emit textRevisionChanged(revision);
    publishDirtyIfChanged();
}

void SharedDocument::publishDirtyIfChanged()
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

void SharedDocument::captureSavedBaseline()
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
    const QFileInfo source(normalizedFileName);
    QFile sourceFile(normalizedFileName);
    if (source.isFile()
        && sourceFile.open(QIODevice::ReadOnly)) {
        savedBaselineDigest =
            QCryptographicHash::hash(
                sourceFile.readAll(),
                QCryptographicHash::Sha256);
    } else {
        savedBaselineDigest =
            QCryptographicHash::hash(
                document->toPlainText().toUtf8(),
                QCryptographicHash::Sha256);
    }
    savedBaselineModifiedTimeUtc =
        source.isFile()
        ? source.lastModified().toUTC()
        : QDateTime();
}

void SharedDocument::applyViewState(
    MyCodeEditor* editor,
    const SharedDocumentViewState& state) const
{
    if (!editor || !editor->document())
        return;
    QTextDocument* viewDocument = editor->document();
    QTextCursor cursor(viewDocument);
    cursor.setPosition(
        boundedDocumentPosition(
            viewDocument,
            state.anchorPosition));
    cursor.setPosition(
        boundedDocumentPosition(
            viewDocument,
            state.cursorPosition),
        QTextCursor::KeepAnchor);
    editor->setTextCursor(cursor);
    if (QScrollBar* bar = editor->verticalScrollBar())
        bar->setValue(qMax(0, state.verticalScrollValue));
    if (QScrollBar* bar = editor->horizontalScrollBar())
        bar->setValue(qMax(0, state.horizontalScrollValue));
}

void SharedDocument::disconnectViewBinding(ViewBinding* binding)
{
    if (!binding)
        return;
    for (const QMetaObject::Connection& connection :
         std::as_const(binding->connections)) {
        QObject::disconnect(connection);
    }
    binding->connections.clear();
}

void SharedDocument::setFileIdentity(
    const QString& documentId,
    const QString& fileName)
{
    id = documentId;
    normalizedFileName =
        EditorFileIdentity::normalized(fileName);
    for (MyCodeEditor* editor : views()) {
        if (editor)
            editor->setDocumentFileName(normalizedFileName);
    }
    emit statusChanged();
}

SharedDocumentRegistry::SharedDocumentRegistry(QObject* parent)
    : QObject(parent)
{
}

SharedDocument* SharedDocumentRegistry::acquire(
    const QString& fileName,
    const QString& initialText)
{
    const QString normalized =
        EditorFileIdentity::normalized(fileName);
    if (normalized.isEmpty())
        return createUntitled(initialText);

    const QString identity =
        EditorFileIdentity::lookupKey(normalized);
    if (SharedDocument* existing =
            documentsByIdentity.value(identity, nullptr)) {
        return existing;
    }
    return createDocument(normalized, normalized, initialText);
}

SharedDocument* SharedDocumentRegistry::createUntitled(
    const QString& initialText)
{
    const QString documentId = newUntitledDocumentId();
    return createDocument(documentId, QString(), initialText);
}

SharedDocument* SharedDocumentRegistry::acquireUntitled(
    const QString& documentId,
    const QString& initialText)
{
    const QString normalizedId =
        documentId.trimmed();
    if (normalizedId.isEmpty())
        return nullptr;
    if (SharedDocument* existing =
            documentsById.value(
                normalizedId,
                nullptr)) {
        return existing->fileName().isEmpty()
            ? existing
            : nullptr;
    }
    return createDocument(
        normalizedId,
        QString(),
        initialText);
}

SharedDocument* SharedDocumentRegistry::documentForFile(
    const QString& fileName) const
{
    return documentsByIdentity.value(
        EditorFileIdentity::lookupKey(fileName), nullptr);
}

SharedDocument* SharedDocumentRegistry::documentForView(
    MyCodeEditor* editor) const
{
    if (!editor)
        return nullptr;
    for (SharedDocument* document : documentsById) {
        if (document && document->views().contains(editor))
            return document;
    }
    return nullptr;
}

SharedDocument* SharedDocumentRegistry::documentById(
    const QString& documentId) const
{
    return documentsById.value(documentId, nullptr);
}

QList<SharedDocument*> SharedDocumentRegistry::documents() const
{
    QList<SharedDocument*> result = documentsById.values();
    std::sort(
        result.begin(),
        result.end(),
        [](const SharedDocument* left,
           const SharedDocument* right) {
            return left->documentId() < right->documentId();
        });
    return result;
}

bool SharedDocumentRegistry::renameDocument(
    SharedDocument* sharedDocument,
    const QString& fileName)
{
    if (!sharedDocument || !documentsById.contains(
            sharedDocument->documentId())) {
        return false;
    }
    const QString normalized =
        EditorFileIdentity::normalized(fileName);
    const QString identity =
        EditorFileIdentity::lookupKey(normalized);
    if (normalized.isEmpty() || identity.isEmpty())
        return false;
    SharedDocument* conflict =
        documentsByIdentity.value(identity, nullptr);
    if (conflict && conflict != sharedDocument)
        return false;

    documentsById.remove(sharedDocument->documentId());
    if (!sharedDocument->fileName().isEmpty()) {
        documentsByIdentity.remove(
            EditorFileIdentity::lookupKey(
                sharedDocument->fileName()));
    }
    sharedDocument->setFileIdentity(normalized, normalized);
    documentsById.insert(normalized, sharedDocument);
    documentsByIdentity.insert(identity, sharedDocument);
    return true;
}

bool SharedDocumentRegistry::releaseIfUnused(
    SharedDocument* sharedDocument)
{
    if (!sharedDocument || sharedDocument->viewCount() != 0)
        return false;
    documentsById.remove(sharedDocument->documentId());
    if (!sharedDocument->fileName().isEmpty()) {
        documentsByIdentity.remove(
            EditorFileIdentity::lookupKey(
                sharedDocument->fileName()));
    }
    delete sharedDocument;
    return true;
}

SharedDocument* SharedDocumentRegistry::createDocument(
    const QString& documentId,
    const QString& fileName,
    const QString& initialText)
{
    auto* sharedDocument = new SharedDocument(
        documentId, fileName, initialText, this);
    documentsById.insert(sharedDocument->documentId(),
                         sharedDocument);
    if (!sharedDocument->fileName().isEmpty()) {
        documentsByIdentity.insert(
            EditorFileIdentity::lookupKey(
                sharedDocument->fileName()),
            sharedDocument);
    }
    return sharedDocument;
}
