#include "workspaceeditdocumentmanager.h"

#include "documentmodel.h"
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

std::optional<QByteArray> diskBytes(const QString& fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return file.readAll();
}

rtledit::DocumentVersion combinedVersion(
    std::uint64_t textRevision,
    std::uint64_t diskFingerprint)
{
    constexpr std::uint64_t mix = 0x9e3779b97f4a7c15ULL;
    return rtledit::DocumentVersion{
        diskFingerprint ^ (textRevision + mix
            + (diskFingerprint << 6)
            + (diskFingerprint >> 2))};
}

bool fileReadOnly(const QString& fileName)
{
    const QFileInfo info(fileName);
    return info.exists() && !info.isWritable();
}

std::uint64_t fingerprintForFile(const QString& fileName)
{
    const std::optional<QByteArray> bytes = diskBytes(fileName);
    return bytes ? contentFingerprint(*bytes) : 0;
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

    const std::uint64_t diskFingerprint =
        fingerprintForFile(fileName);
    if (tabs && tabs->getDocumentModel()) {
        DocumentModel* model = tabs->getDocumentModel();
        if (MyCodeEditor* editor = model->editorForFile(fileName)) {
            const DocumentSnapshot metadata =
                model->cachedDocumentForFile(fileName);
            return rtledit::WorkspaceDocumentSnapshot{
                combinedVersion(
                    static_cast<std::uint64_t>(
                        qMax(0, metadata.textVersion)),
                    diskFingerprint),
                utf8String(editor->cachedDocumentText())};
        }
    }

    const std::optional<QByteArray> bytes = diskBytes(fileName);
    if (!bytes)
        return std::nullopt;
    return rtledit::WorkspaceDocumentSnapshot{
        combinedVersion(0, contentFingerprint(*bytes)),
        std::string(bytes->constData(),
                    static_cast<std::size_t>(bytes->size()))};
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
    DocumentModel* model = tabs->getDocumentModel();
    if (!model)
        return false;
    MyCodeEditor* editor = model->editorForFile(fileName);
    if (!editor) {
        if (!tabs->openFileInTab(fileName))
            return false;
        editor = model->editorForFile(fileName);
    }
    if (!editor || editor->isReadOnly())
        return false;

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

    {
        auto transaction =
            editor->beginSynchronousEditTransaction();
        QTextCursor cursor(editor->document());
        cursor.beginEditBlock();
        for (const QtEdit& edit : std::as_const(qtEdits)) {
            cursor.setPosition(edit.start);
            cursor.setPosition(edit.end,
                               QTextCursor::KeepAnchor);
            cursor.insertText(edit.replacement);
        }
        cursor.endEditBlock();
    }
    tabs->updateTabTitle(editor);
    return utf8String(editor->cachedDocumentText())
        == *expectedAfter;
}

bool WorkspaceEditDocumentManager::restoreSnapshot(
    const std::string& filePath,
    const rtledit::WorkspaceDocumentSnapshot& snapshot)
{
    if (!tabs)
        return false;
    const QString fileName = EditorFileIdentity::normalized(
        fromUtf8String(filePath));
    DocumentModel* model = tabs->getDocumentModel();
    if (fileName.isEmpty() || !model)
        return false;
    MyCodeEditor* editor = model->editorForFile(fileName);
    if (!editor) {
        if (!tabs->openFileInTab(fileName))
            return false;
        editor = model->editorForFile(fileName);
    }
    if (!editor || editor->isReadOnly())
        return false;

    const QString restored = fromUtf8String(snapshot.text);
    if (editor->cachedDocumentText() == restored)
        return true;
    {
        auto transaction =
            editor->beginSynchronousEditTransaction();
        QTextCursor cursor(editor->document());
        cursor.beginEditBlock();
        cursor.select(QTextCursor::Document);
        cursor.insertText(restored);
        cursor.endEditBlock();
    }
    tabs->updateTabTitle(editor);
    return editor->cachedDocumentText() == restored;
}
