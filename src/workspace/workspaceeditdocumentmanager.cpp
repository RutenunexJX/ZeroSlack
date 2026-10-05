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

bool WorkspaceEditDocumentManager::applyTextEdits(
    const std::string& filePath,
    rtledit::DocumentVersion expectedVersion,
    const std::vector<rtledit::WorkspaceTextEdit>& edits)
{
    if (!tabs)
        return false;
    const QString fileName = EditorFileIdentity::normalized(
        fromUtf8String(filePath));
    if (fileName.isEmpty() || fileReadOnly(fileName))
        return false;
    if (!tabs->sharedDocuments) return false;
    const auto current = snapshot(filePath);
    if (!current || current->version != expectedVersion)
        return false;

    const std::string& before = current->text;
    struct QtEdit {
        int start = 0;
        int end = 0;
        QString replacement;
    };
    QList<QtEdit> qtEdits;
    qtEdits.reserve(static_cast<qsizetype>(edits.size()));
    for (const auto& edit : edits) {
        const auto offsets =
            rtledit::rangeToOffsets(before, edit.range);
        if (!offsets
            || offsets->start
                > static_cast<std::size_t>(
                    std::numeric_limits<int>::max())
            || offsets->end
                > static_cast<std::size_t>(
                    std::numeric_limits<int>::max())) {
            return false;
        }
        const QByteArray startBytes(
            before.data(), static_cast<qsizetype>(offsets->start));
        const QByteArray endBytes(
            before.data(), static_cast<qsizetype>(offsets->end));
        const QString startPrefix = QString::fromUtf8(startBytes);
        const QString endPrefix = QString::fromUtf8(endBytes);
        if (startPrefix.toUtf8().size()
                != static_cast<qsizetype>(offsets->start)
            || endPrefix.toUtf8().size()
                != static_cast<qsizetype>(offsets->end)) {
            return false;
        }
        qtEdits.append(QtEdit{
            static_cast<int>(startPrefix.size()),
            static_cast<int>(endPrefix.size()),
            fromUtf8String(edit.newText)});
    }

    const auto expectedAfter =
        rtledit::applyTextEditsToString(before, edits);
    if (!expectedAfter)
        return false;

    auto* document = tabs->sharedDocuments->documentForFile(fileName);
    const bool newlyLoaded = !document;
    if (!document) document = tabs->acquireFileDocument(fileName);
    if (!document || document->readOnly()) return false;
    // First acquisition changes the buffer's lifetime identity, not its text.
    // Recheck exact disk generation and logical text before accepting promotion.
    const auto source = readDocumentFile(fileName);
    const auto version = combinedVersion(document->textRevision(),
        source.available ? contentFingerprint(source.rawBytes) : 0,
        newlyLoaded ? 0 : document->instanceSerial());
    if (version != expectedVersion || utf8String(document->textDocument()->toPlainText()) != before
        || (newlyLoaded && (!source.available || document->textRevision() != 0))) {
        if (newlyLoaded) tabs->sharedDocuments->releaseIfUnused(document);
        return false;
    }
    {
        auto guards = guardViews(document);
        QTextCursor cursor(document->textDocument());
        cursor.beginEditBlock();
        for (const QtEdit& edit : std::as_const(qtEdits)) {
            cursor.setPosition(edit.start);
            cursor.setPosition(edit.end, QTextCursor::KeepAnchor);
            cursor.insertText(edit.replacement);
        }
        cursor.endEditBlock();
    }
    const bool applied = utf8String(document->textDocument()->toPlainText()) == *expectedAfter;
    // Preserve the user-facing unsaved-file review after a successful edit.
    // Invalid plans and snapshot access never create visible tabs.
    if (applied && document->viewCount() == 0) tabs->openFileInTab(fileName);
    for (auto* view : document->views()) tabs->updateTabTitle(view);
    return applied;
}

bool WorkspaceEditDocumentManager::restoreSnapshot(
    const std::string& filePath,
    const rtledit::WorkspaceDocumentSnapshot& snapshot)
{
    if (!tabs)
        return false;
    const QString fileName = EditorFileIdentity::normalized(
        fromUtf8String(filePath));
    if (fileName.isEmpty() || !tabs->sharedDocuments || fileReadOnly(fileName)) return false;
    auto* document = tabs->sharedDocuments->documentForFile(fileName);
    if (!document) document = tabs->acquireFileDocument(fileName);
    if (!document || document->readOnly()) return false;
    const auto restored = fromUtf8String(snapshot.text);
    if (document->textDocument()->toPlainText() != restored) {
        auto guards = guardViews(document);
        QTextCursor cursor(document->textDocument());
        cursor.beginEditBlock();
        cursor.select(QTextCursor::Document);
        cursor.insertText(restored);
        cursor.endEditBlock();
    }
    const bool succeeded = document->textDocument()->toPlainText() == restored;
    for (auto* view : document->views()) tabs->updateTabTitle(view);
    if (succeeded && document->viewCount() == 0) {
        // A failed apply may have acquired an unopened document without edits.
        // Clean rollback is not an instruction to open a new presentation.
        const auto disk = readDocumentFile(fileName);
        if (disk.available && disk.text == restored) tabs->sharedDocuments->releaseIfUnused(document);
        else tabs->openFileInTab(fileName);
    }
    return succeeded;
}
