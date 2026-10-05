#include "workspaceeditdocumentmanager.h"

#include "documentmodel.h"
#include "shareddocument.h"
#include <zeroslack/documents/documentfileread.h>
#include <QTextDocument>
#include <memory>
#include "editorfileidentity.h"
#include "mycodeeditor.h"
#include "tabmanager.h"

#include <rtledit/text_edit.h>

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QTextCursor>

#include <algorithm>
#include <cstdint>
#include <limits>

namespace {

std::string utf8String(const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    return std::string(bytes.constData(),
                       static_cast<std::size_t>(bytes.size()));
}

QString fromUtf8String(const std::string& text)
{
    return QString::fromUtf8(
        text.data(), static_cast<qsizetype>(text.size()));
}

std::uint64_t contentFingerprint(const QByteArray& bytes)
{
    constexpr std::uint64_t offset = 1469598103934665603ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    std::uint64_t hash = offset;
    for (const char byte : bytes) {
        hash ^= static_cast<unsigned char>(byte);
        hash *= prime;
    }
    return hash;
}

rtledit::DocumentVersion combinedVersion(std::uint64_t textRevision,
                                          std::uint64_t diskFingerprint,
                                          std::uint64_t serial = 0)
{
    constexpr std::uint64_t mix = 0x9e3779b97f4a7c15ULL;
    return {diskFingerprint ^ (textRevision + mix + (diskFingerprint << 6)
        + (diskFingerprint >> 2)) ^ (serial * mix)};
}

using EditGuards = std::vector<std::unique_ptr<MyCodeEditor::SynchronousEditTransaction>>;
EditGuards guardViews(SharedDocument* document)
{
    EditGuards guards;
    for (auto* view : document->views())
        guards.emplace_back(new MyCodeEditor::SynchronousEditTransaction(view->beginSynchronousEditTransaction()));
    return guards;
}

bool fileReadOnly(const QString& fileName)
{
    const QFileInfo info(fileName);
    return info.exists() && !info.isWritable();
}

} // namespace

WorkspaceEditDocumentManager::WorkspaceEditDocumentManager(
    TabManager* tabManager)
    : tabs(tabManager)
{
}

std::optional<rtledit::WorkspaceDocumentSnapshot>
WorkspaceEditDocumentManager::snapshot(
    const std::string& filePath) const
{
    const QString fileName = EditorFileIdentity::normalized(
        fromUtf8String(filePath));
    if (fileName.isEmpty())
        return std::nullopt;

    const auto source = readDocumentFile(fileName);
    const auto fingerprint = source.available ? contentFingerprint(source.rawBytes) : 0;
    auto* document = tabs && tabs->sharedDocuments ? tabs->sharedDocuments->documentForFile(fileName) : nullptr;
    if (document) {
        return rtledit::WorkspaceDocumentSnapshot{
            combinedVersion(document->textRevision(), fingerprint, document->instanceSerial()),
            utf8String(document->textDocument()->toPlainText())};
    }
    if (!source.available) return std::nullopt;
    return rtledit::WorkspaceDocumentSnapshot{combinedVersion(0, fingerprint), utf8String(source.text)};
}

rtledit::DocumentMutationResult WorkspaceEditDocumentManager::applyTextEdits(
    const std::string& filePath, rtledit::DocumentVersion expectedVersion,
    const std::vector<rtledit::WorkspaceTextEdit>& edits)
{
    const auto current = snapshot(filePath);
    if (!current || current->version != expectedVersion) return {};
    rtledit::TextCoordinateIndex index(current->text);
    std::vector<rtledit::IndexedWorkspaceTextEdit> indexed;
    indexed.reserve(edits.size());
    for (const auto& edit : edits) {
        const auto offsets = index.offsets(edit.range);
        if (!offsets) return {};
        indexed.push_back({edit, indexed.size(), *offsets});
    }
    return applyPreparedTextEdits(filePath, *current, indexed);
}

rtledit::DocumentMutationResult WorkspaceEditDocumentManager::applyPreparedTextEdits(
    const std::string& filePath, const rtledit::WorkspaceDocumentSnapshot& expected,
    const std::vector<rtledit::IndexedWorkspaceTextEdit>& edits)
{
    if (!tabs || !tabs->sharedDocuments) return {};
    const QString fileName = EditorFileIdentity::normalized(fromUtf8String(filePath));
    if (fileName.isEmpty() || fileReadOnly(fileName)) return {};
    const auto current = snapshot(filePath);
    if (!current || current->version != expected.version || current->text != expected.text) return {};
    const auto& before = current->text;
    struct QtEdit { int start = 0; int end = 0; QString replacement; };
    struct Endpoint { std::size_t offset; std::size_t edit; bool end; };
    std::vector<QtEdit> qtEdits(edits.size());
    std::vector<Endpoint> endpoints; endpoints.reserve(edits.size() * 2);
    for (std::size_t i = 0; i < edits.size(); ++i) {
        const auto& indexed = edits[i];
        if (indexed.offsets.start > indexed.offsets.end || indexed.offsets.end > before.size()
            || before.compare(indexed.offsets.start, indexed.offsets.end - indexed.offsets.start,
                              indexed.edit.expectedText) != 0) return {};
        endpoints.push_back({indexed.offsets.start, i, false});
        endpoints.push_back({indexed.offsets.end, i, true});
        qtEdits[i].replacement = fromUtf8String(indexed.edit.newText);
        if (qtEdits[i].replacement.toUtf8()
            != QByteArrayView(indexed.edit.newText.data(), static_cast<qsizetype>(indexed.edit.newText.size()))) return {};
    }
    std::sort(endpoints.begin(), endpoints.end(), [](const auto& a, const auto& b) { return a.offset < b.offset; });
    std::size_t previous = 0;
    qsizetype utf16 = 0;
    for (const auto& endpoint : endpoints) {
        const auto count = static_cast<qsizetype>(endpoint.offset - previous);
        const auto chunk = QString::fromUtf8(before.data() + previous, count);
        // Reject split UTF-8 sequences and invalid byte input. Across all
        // endpoints each byte is decoded once; no whole-prefix temporaries.
        if (chunk.toUtf8() != QByteArrayView(before.data() + previous, count)) return {};
        utf16 += chunk.size(); previous = endpoint.offset;
        if (utf16 > std::numeric_limits<int>::max()) return {};
        (endpoint.end ? qtEdits[endpoint.edit].end : qtEdits[endpoint.edit].start) = static_cast<int>(utf16);
    }
    const auto expectedAfter = rtledit::applyIndexedTextEditsToString(before, edits);
    if (!expectedAfter) return {};
    auto* document = tabs->sharedDocuments->documentForFile(fileName);
    const bool newlyLoaded = !document;
    if (!document) document = tabs->acquireFileDocument(fileName);
    if (!document || document->readOnly()) return {};
    const auto source = readDocumentFile(fileName);
    const auto diskFingerprint = source.available ? contentFingerprint(source.rawBytes) : 0;
    const auto version = combinedVersion(document->textRevision(), diskFingerprint,
        newlyLoaded ? 0 : document->instanceSerial());
    if (version != expected.version || utf8String(document->textDocument()->toPlainText()) != before
        || !document->matchesSourcePath(fileName)
        || (newlyLoaded && (!source.available || document->textRevision() != 0))) {
        if (newlyLoaded) tabs->sharedDocuments->releaseIfUnused(document);
        return {};
    }
    const auto originalRevision = document->textRevision();
    {
        auto guards = guardViews(document);
        QTextCursor cursor(document->textDocument()); cursor.beginEditBlock();
        for (const auto& edit : qtEdits) {
            cursor.setPosition(edit.start); cursor.setPosition(edit.end, QTextCursor::KeepAnchor);
            cursor.insertText(edit.replacement);
        }
        cursor.endEditBlock();
    }
    rtledit::WorkspaceDocumentSnapshot owned{
        combinedVersion(document->textRevision(), diskFingerprint, document->instanceSerial()),
        utf8String(document->textDocument()->toPlainText())};
    const bool applied = owned.text == *expectedAfter;
    const bool modified = document->textRevision() != originalRevision;
    // A reentrant observer may have edited the same buffer. Do not claim its
    // text as our post-state merely because we can read it now.
    if (!applied) return {false, modified, std::nullopt};
    if (applied && document->viewCount() == 0) tabs->openFileInTab(fileName);
    for (auto* view : document->views()) tabs->updateTabTitle(view);
    return {applied, modified, std::move(owned)};
}

rtledit::DocumentMutationResult WorkspaceEditDocumentManager::restoreSnapshot(
    const std::string& filePath, const rtledit::WorkspaceDocumentSnapshot& expectedCurrent,
    const rtledit::WorkspaceDocumentSnapshot& target)
{
    if (!tabs || !tabs->sharedDocuments) return {};
    const QString fileName = EditorFileIdentity::normalized(fromUtf8String(filePath));
    if (fileName.isEmpty() || fileReadOnly(fileName)) return {};
    const auto current = snapshot(filePath);
    if (!current || current->version != expectedCurrent.version || current->text != expectedCurrent.text) return {};
    auto* document = tabs->sharedDocuments->documentForFile(fileName);
    const bool newlyLoaded = !document;
    if (!document) document = tabs->acquireFileDocument(fileName);
    if (!document || document->readOnly()) return {};
    const auto source = readDocumentFile(fileName);
    const auto diskFingerprint = source.available ? contentFingerprint(source.rawBytes) : 0;
    const auto version = combinedVersion(document->textRevision(), diskFingerprint,
        newlyLoaded ? 0 : document->instanceSerial());
    if (version != expectedCurrent.version || utf8String(document->textDocument()->toPlainText()) != expectedCurrent.text
        || !document->matchesSourcePath(fileName)) {
        if (newlyLoaded) tabs->sharedDocuments->releaseIfUnused(document);
        return {};
    }
    const auto originalRevision = document->textRevision();
    const auto restored = fromUtf8String(target.text);
    if (document->textDocument()->toPlainText() != restored) {
        auto guards = guardViews(document);
        QTextCursor cursor(document->textDocument()); cursor.beginEditBlock();
        cursor.select(QTextCursor::Document); cursor.insertText(restored); cursor.endEditBlock();
    }
    const bool succeeded = document->textDocument()->toPlainText() == restored;
    const bool modified = document->textRevision() != originalRevision;
    rtledit::WorkspaceDocumentSnapshot owned{
        combinedVersion(document->textRevision(), diskFingerprint, document->instanceSerial()),
        utf8String(document->textDocument()->toPlainText())};
    if (!succeeded) return {false, modified, std::nullopt};
    for (auto* view : document->views()) tabs->updateTabTitle(view);
    if (succeeded && document->viewCount() == 0) {
        if (source.available && source.text == restored) {
            if (tabs->sharedDocuments->releaseIfUnused(document)) owned.version = combinedVersion(0, diskFingerprint);
        } else tabs->openFileInTab(fileName);
    }
    return {succeeded, modified, std::move(owned)};
}
